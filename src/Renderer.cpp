#include "Renderer.h"
#include "DX12Helper.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <dxcapi.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <cstring>

// PIX support for GPU debugging (DEBUG only)
#if defined(_DEBUG) && defined(USE_PIX)
#include <pix3.h>
#else
// Define PIX macros as no-ops if PIX is not available
#define PIXBeginEvent(...)
#define PIXEndEvent(...)
#define PIXSetMarker(...)
#endif

#pragma comment(lib, "dxcompiler.lib")

// Note: The DXR helper includes have been removed for now to resolve compilation issues.
// We will build the DXR pipeline using the core D3D12 interfaces first.
// #include "dxr/DXRHelper.h"
// #include "dxr/nv_helpers_dx12/BottomLevelASGenerator.h"
// #include "dxr/nv_helpers_dx12/TopLevelASGenerator.h"
// #include "dxr/nv_helpers_dx12/RaytracingPipelineGenerator.h"
// #include "dxr/nv_helpers_dx12/RootSignatureGenerator.h"
// #include "dxr/nv_helpers_dx12/ShaderBindingTableGenerator.h"


namespace ACG {

    Renderer::Renderer(UINT width, UINT height) :
        m_width(width),
        m_height(height),
        m_frameIndex(0),
        m_fenceValue(0),
        m_rtvDescriptorSize(0),
        m_hwnd(nullptr),
        m_dxrSupported(false),
        m_sbtEntrySize(0),
        m_sbtRayGenOffset(0),
        m_sbtMissOffset(0),
        m_sbtHitGroupOffset(0),
        m_samplesPerPixel(1),
        m_maxBounces(5),
        m_accumulatedSamples(0),
        m_isRendering(false),
        m_offlineFenceValue(0),
        m_offlineFenceEvent(nullptr),
        m_stopRenderRequested(false),
        m_useVirtualTextures(false)
    {
        // 初始化OIDN降噪器
        m_denoiser = std::make_unique<Denoiser>();
        if (!m_denoiser->Initialize()) {
            std::cerr << "Warning: Failed to initialize denoiser: " << m_denoiser->GetError() << std::endl;
        }
    }

    Renderer::~Renderer() {
        OnDestroy();
        if (m_offlineFenceEvent) {
            CloseHandle(m_offlineFenceEvent);
        }
    }

    void Renderer::OnInit(HWND hwnd) {
        m_hwnd = hwnd;
        InitPipeline(hwnd);
        CheckRaytracingSupport();
        if (m_dxrSupported) {
            CreateRaytracingPipeline();
        } else {
            std::cerr << "WARNING: DirectX Raytracing is not supported on this device!" << std::endl;
            std::cerr << "The application will run without ray tracing." << std::endl;
        }
    }

    void Renderer::LoadScene(const std::string& path) {
        try {
            m_scene = std::make_unique<Scene>();
            m_scene->LoadFromFile(path);
            
            // Prepare command list for resource creation
            WaitForGpu(); // Ensure GPU is idle
            ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset(), "Failed to reset command allocator");
            ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "Failed to reset command list");
            
            // Create resources (these functions will record commands)
            CreateShaderResources(m_commandList.Get());
            CreateAccelerationStructures(m_commandList.Get());
            CreateShaderBindingTable();
            
            // Execute and wait
            ThrowIfFailed(m_commandList->Close(), "Failed to close command list");
            ID3D12CommandList* lists[] = { m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(1, lists);
            WaitForGpu();
            
            std::cout << "Scene loaded successfully" << std::endl;
        } catch (const std::runtime_error& e) {
            std::cerr << "Failed to load scene: " << e.what() << std::endl;
            throw;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load scene: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "Failed to load scene: Unknown error" << std::endl;
            throw std::runtime_error("Unknown error occurred while loading scene");
        }
    }

    void Renderer::LoadSceneAsync(const std::string& path) {
        try {
            std::cout << "[Async] Loading scene from file..." << std::endl;
            std::cout.flush();
            
            m_scene = std::make_unique<Scene>();
            bool loadSuccess = m_scene->LoadFromFile(path);
            
            if (!loadSuccess) {
                throw std::runtime_error("Scene loading failed");
            }
            
            // Display loading statistics
            const auto& stats = m_scene->GetLoadStats();
            std::cout << "[Async] Scene loaded: " << stats.totalMeshes << " meshes, " 
                      << stats.totalTriangles << " triangles, " 
                      << stats.totalVertices << " vertices" << std::endl;
            std::cout << "[Async] Estimated memory: " << stats.estimatedMemoryMB << " MB" << std::endl;
            
            std::cout << "[Async] Creating shader resources..." << std::endl;
            std::cout.flush();
            
            // Create independent command allocator for async loading
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> loadAllocator;
            ThrowIfFailed(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&loadAllocator)),
                "Failed to create load command allocator");
            
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> tempCommandList;
            ThrowIfFailed(m_device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                loadAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(&tempCommandList)),
                "Failed to create load command list");
            
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> loadCommandList;
            ThrowIfFailed(tempCommandList.As(&loadCommandList),
                "Failed to query ID3D12GraphicsCommandList4 interface");
            
            // Close the command list (it was created in open state)
            ThrowIfFailed(loadCommandList->Close(), "Failed to close new command list");
            
            // Reset the command allocator and list to prepare for recording
            ThrowIfFailed(loadAllocator->Reset(), "Failed to reset load allocator");
            ThrowIfFailed(loadCommandList->Reset(loadAllocator.Get(), nullptr), "Failed to reset load command list");
            
            // Create resources with independent command list (no swapping needed!)
            CreateShaderResources(loadCommandList.Get());
            CreateAccelerationStructures(loadCommandList.Get());
            CreateShaderBindingTable();
            
            // Now close and execute the command list
            ThrowIfFailed(loadCommandList->Close(), "Failed to close load command list");
            ID3D12CommandList* lists[] = { loadCommandList.Get() };
            m_commandQueue->ExecuteCommandLists(1, lists);
            
            // Wait for all GPU operations to complete
            WaitForGpu();
            
            std::cout << "[Async] Scene loaded successfully" << std::endl;
            std::cout.flush();
        } catch (const std::runtime_error& e) {
            std::cerr << "[Async] Failed to load scene: " << e.what() << std::endl;
            std::cerr.flush();
            throw;
        } catch (const std::exception& e) {
            std::cerr << "[Async] Failed to load scene: " << e.what() << std::endl;
            std::cerr.flush();
            throw;
        } catch (...) {
            std::cerr << "[Async] Failed to load scene: Unknown error" << std::endl;
            std::cerr.flush();
            throw std::runtime_error("Unknown error occurred while loading scene");
        }
    }

    void Renderer::RenderToFile(const std::string& outputPath, int samplesPerPixel, int maxBounces) {
        // RAII guard to ensure m_isRendering is always cleared
        struct RenderGuard {
            std::atomic<bool>& flag;
            RenderGuard(std::atomic<bool>& f) : flag(f) { flag.store(true); }
            ~RenderGuard() { flag.store(false); }
        };
        RenderGuard guard(m_isRendering);
        
        try {
            m_stopRenderRequested = false;
            std::cout << "Starting offline render to " << outputPath << "..." << std::endl;
            std::cout << "Resolution: " << m_width << "x" << m_height 
                      << ", Samples: " << samplesPerPixel 
                      << ", Bounces: " << maxBounces << std::endl;

            // Ensure scene is loaded
            if (!m_scene || m_scene->GetMeshes().empty()) {
                throw std::runtime_error("Scene is not loaded or is empty.");
            }

            // Create independent command resources for offline rendering
            if (!m_offlineCommandAllocator) {
                std::cout << "Creating offline command allocator..." << std::endl;
                HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_offlineCommandAllocator));
                if (FAILED(hr)) {
                    char errMsg[256];
                    sprintf_s(errMsg, "Failed to create offline command allocator (HRESULT: 0x%08X)", hr);
                    throw std::runtime_error(errMsg);
                }
            }
            
            if (!m_offlineFence) {
                std::cout << "Creating offline fence..." << std::endl;
                HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_offlineFence));
                if (FAILED(hr)) {
                    char errMsg[256];
                    sprintf_s(errMsg, "Failed to create offline fence (HRESULT: 0x%08X)", hr);
                    throw std::runtime_error(errMsg);
                }
                m_offlineFenceValue = 1;
                m_offlineFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!m_offlineFenceEvent) {
                    throw std::runtime_error("Failed to create offline fence event");
                }
            }

            // Reset and get command list
            std::cout << "Resetting command allocator..." << std::endl;
            HRESULT hr = m_offlineCommandAllocator->Reset();
            if (FAILED(hr)) {
                char errMsg[256];
                sprintf_s(errMsg, "Failed to reset offline command allocator (HRESULT: 0x%08X)", hr);
                throw std::runtime_error(errMsg);
            }
            
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> renderCommandList;
            std::cout << "Creating command list..." << std::endl;
            hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_offlineCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&renderCommandList));
            if (FAILED(hr)) {
                char errMsg[256];
                sprintf_s(errMsg, "Failed to create command list (HRESULT: 0x%08X)", hr);
                throw std::runtime_error(errMsg);
            }
            
            // Set pipeline state
            renderCommandList->SetPipelineState1(m_dxrStateObject.Get());
            renderCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());

            // Set heaps
            ID3D12DescriptorHeap* ppHeaps[] = { m_srvUavHeap.Get() };
            renderCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            // DEBUG: Print binding info
            // Set root arguments
            // Root parameter 0: UAV table (output texture at index 0)
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            uavHandle.ptr += m_uavIndex_Output * m_srvUavDescriptorSize;
            renderCommandList->SetComputeRootDescriptorTable(0, uavHandle);
            
            // Root parameter 1: TLAS (direct SRV, not a table)
            renderCommandList->SetComputeRootShaderResourceView(1, m_topLevelAS->GetGPUVirtualAddress());
            
            // Root parameter 2: Vertices SRV table (index 1)
            D3D12_GPU_DESCRIPTOR_HANDLE verticesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            verticesHandle.ptr += m_srvIndex_Vertices * m_srvUavDescriptorSize;
            renderCommandList->SetComputeRootDescriptorTable(2, verticesHandle);
            
            // Root parameter 3: Indices SRV table (index 2)
            D3D12_GPU_DESCRIPTOR_HANDLE indicesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            indicesHandle.ptr += m_srvIndex_Indices * m_srvUavDescriptorSize;
            renderCommandList->SetComputeRootDescriptorTable(3, indicesHandle);
            
            // Root parameter 4: Triangle materials SRV table (index 3)
            D3D12_GPU_DESCRIPTOR_HANDLE triMatHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            triMatHandle.ptr += 3 * m_srvUavDescriptorSize;
            renderCommandList->SetComputeRootDescriptorTable(4, triMatHandle);
            
            // Root parameter 5: Materials SRV (direct root descriptor)
            D3D12_GPU_VIRTUAL_ADDRESS materialsAddress = m_materialBuffer->GetGPUVirtualAddress();
            renderCommandList->SetComputeRootShaderResourceView(5, materialsAddress);
            
            // Root parameter 10: Material Layers SRV table (bind to descriptor slot 5)
            D3D12_GPU_DESCRIPTOR_HANDLE layersHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            layersHandle.ptr += m_srvIndex_MaterialLayers * m_srvUavDescriptorSize;
            renderCommandList->SetComputeRootDescriptorTable(10, layersHandle);
            
            // Root parameter 6: Textures SRV table (t3, bind to descriptor slot 5)
            D3D12_GPU_DESCRIPTOR_HANDLE texturesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            texturesHandle.ptr += 5 * m_srvUavDescriptorSize; // slot 5 for texture array
            renderCommandList->SetComputeRootDescriptorTable(6, texturesHandle);
            
            // Root parameter 7: Environment map SRV table (t4, bind to descriptor slot 6)
            D3D12_GPU_DESCRIPTOR_HANDLE envMapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            envMapHandle.ptr += 6 * m_srvUavDescriptorSize; // slot 6 for environment map
            renderCommandList->SetComputeRootDescriptorTable(7, envMapHandle);
            
            // Root parameter 8: Virtual Texture Cache SRV table (bind to descriptor slot 7, t5)
            if (m_useVirtualTextures) {
                D3D12_GPU_DESCRIPTOR_HANDLE vtCacheHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                vtCacheHandle.ptr += 7 * m_srvUavDescriptorSize; // slot 7 for virtual texture cache
                renderCommandList->SetComputeRootDescriptorTable(8, vtCacheHandle);
                
                // Also bind indirection texture immediately
                D3D12_GPU_DESCRIPTOR_HANDLE indirectionHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                indirectionHandle.ptr += 8 * m_srvUavDescriptorSize; // slot 8 for indirection texture
                renderCommandList->SetComputeRootDescriptorTable(9, indirectionHandle);
            }
            
            // Root parameter 11: Texture Scales SRV table (t8, bind to descriptor slot 9)
            D3D12_GPU_DESCRIPTOR_HANDLE scalesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
            scalesHandle.ptr += 9 * m_srvUavDescriptorSize; // slot 9 for texture scales
            renderCommandList->SetComputeRootDescriptorTable(11, scalesHandle);

            // Root parameter 12: Camera constants (32-bit constants)
            // Compute camera matrices
            glm::vec3 pos = m_camera.GetPosition();
            glm::vec3 dir = m_camera.GetDirection();
            glm::vec3 right = m_camera.GetRight();
            glm::vec3 up = m_camera.GetUp();
            
            // Build camera-to-world matrix (view inverse)
            // GLM matrices are COLUMN-MAJOR, so mat[i] is the i-th COLUMN
            glm::mat4 cameraToWorld = glm::mat4(1.0f);
            cameraToWorld[0] = glm::vec4(right, 0.0f);      // X axis (column 0)
            cameraToWorld[1] = glm::vec4(up, 0.0f);         // Y axis (column 1)
            cameraToWorld[2] = glm::vec4(-dir, 0.0f);       // Z axis (column 2) - camera looks along -Z
            cameraToWorld[3] = glm::vec4(pos, 1.0f);        // Translation (column 3)
            
            // Transpose for row-major HLSL (DirectX uses row-major by default)
            cameraToWorld = glm::transpose(cameraToWorld);
            
            // Get projection matrix and invert it, then transpose
            glm::mat4 projMatrix = m_camera.GetProjectionMatrix();
            glm::mat4 projInverse = glm::transpose(glm::inverse(projMatrix));
            
            CameraConstants cameraConstants;
            cameraConstants.viewInverse = cameraToWorld;
            cameraConstants.projInverse = projInverse;
            cameraConstants.frameIndex = static_cast<uint32_t>(m_accumulatedSamples);
            cameraConstants.maxBounces = static_cast<uint32_t>(m_maxBounces);
            cameraConstants.environmentLightIntensity = m_environmentLightIntensity;
            cameraConstants.padding = 0.0f;
            cameraConstants.cameraParams = glm::vec4(
                m_camera.GetFOV(),
                static_cast<float>(m_width) / static_cast<float>(m_height),
                m_camera.GetAperture(),
                m_camera.GetFocusDistance()
            );
            cameraConstants.sunDirIntensity = glm::vec4(m_sunDirection, m_sunIntensity);
            cameraConstants.sunColorEnabled = glm::vec4(m_sunColor, 1.0f);  // Always 1.0, controlled by intensity
            
            // Set root constants (CameraConstants size in DWORDs) - ROOT PARAMETER 12
            renderCommandList->SetComputeRoot32BitConstants(12, sizeof(CameraConstants) / 4, &cameraConstants, 0);

            // Dispatch rays with accumulation
            D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
            dispatchDesc.RayGenerationShaderRecord.StartAddress = m_sbtBuffer->GetGPUVirtualAddress() + m_sbtRayGenOffset;
            dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_sbtEntrySize;
            dispatchDesc.MissShaderTable.StartAddress = m_sbtBuffer->GetGPUVirtualAddress() + m_sbtMissOffset;
            dispatchDesc.MissShaderTable.SizeInBytes = m_sbtEntrySize; // Assuming one miss shader
            dispatchDesc.MissShaderTable.StrideInBytes = m_sbtEntrySize;
            dispatchDesc.HitGroupTable.StartAddress = m_sbtBuffer->GetGPUVirtualAddress() + m_sbtHitGroupOffset;
            dispatchDesc.HitGroupTable.SizeInBytes = m_sbtEntrySize; // Single hit group for all geometry
            dispatchDesc.HitGroupTable.StrideInBytes = m_sbtEntrySize;
            dispatchDesc.Width = m_width;
            dispatchDesc.Height = m_height;
            dispatchDesc.Depth = 1;

            std::cout << "Starting progressive rendering loop..." << std::endl;
            
            // PIX: Mark render loop start
            PIXBeginEvent(renderCommandList.Get(), PIX_COLOR_INDEX(1), "Path Tracing Loop");
            
            // Clear output texture before first sample to ensure clean start
            D3D12_RECT clearRect = { 0, 0, (LONG)m_width, (LONG)m_height };
            float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
            renderCommandList->ClearUnorderedAccessViewFloat(
                m_srvUavHeap->GetGPUDescriptorHandleForHeapStart(),
                CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvUavHeap->GetCPUDescriptorHandleForHeapStart(), m_uavIndex_Output, m_srvUavDescriptorSize),
                m_outputTexture.Get(),
                clearColor,
                1, &clearRect
            );
            
            // Barrier after clear
            D3D12_RESOURCE_BARRIER initialBarrier = {};
            initialBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            initialBarrier.UAV.pResource = m_outputTexture.Get();
            renderCommandList->ResourceBarrier(1, &initialBarrier);
            
            // Reset accumulated samples counter
            m_accumulatedSamples = 0;

            // Render in batches to allow progress updates
            const int batchSize = 10; // Execute GPU work every 10 samples
            
            for (int sampleIdx = 0; sampleIdx < samplesPerPixel; ++sampleIdx) {
                // Check if stop was requested
                if (m_stopRenderRequested) {
                    std::cout << "Render stopped by user at sample " << (sampleIdx + 1) << "/" << samplesPerPixel << std::endl;
                    std::cout.flush();
                    renderCommandList->Close();
                    return;
                }
                
                // Progress output with flush to ensure visibility
                if (sampleIdx == 0 || (sampleIdx + 1) % 10 == 0 || sampleIdx == samplesPerPixel - 1) {
                    std::cout << "  Sample " << (sampleIdx + 1) << "/" << samplesPerPixel << " starting..." << std::endl;
                    std::cout.flush();
                }
                
                // Update camera constants with current sample index
                CameraConstants cameraConstants;
                cameraConstants.viewInverse = cameraToWorld;
                cameraConstants.projInverse = projInverse;
                cameraConstants.frameIndex = static_cast<uint32_t>(sampleIdx); // Current sample index for accumulation
                cameraConstants.maxBounces = static_cast<uint32_t>(maxBounces);
                cameraConstants.environmentLightIntensity = m_environmentLightIntensity;
                cameraConstants.padding = 0.0f;
                cameraConstants.cameraParams = glm::vec4(
                    m_camera.GetFOV(),
                    static_cast<float>(m_width) / static_cast<float>(m_height),
                    m_camera.GetAperture(),
                    m_camera.GetFocusDistance()
                );
                cameraConstants.sunDirIntensity = glm::vec4(m_sunDirection, m_sunIntensity);
                cameraConstants.sunColorEnabled = glm::vec4(m_sunColor, 1.0f);  // Always 1.0, controlled by intensity
                renderCommandList->SetComputeRoot32BitConstants(12, sizeof(CameraConstants) / 4, &cameraConstants, 0);
                
                // PIX: Mark individual sample
                PIXBeginEvent(renderCommandList.Get(), PIX_COLOR_INDEX(2), "Sample %d", sampleIdx + 1);
                
                // Dispatch rays for this sample
                renderCommandList->DispatchRays(&dispatchDesc);
                
                PIXEndEvent(renderCommandList.Get());
                
                // Add UAV barrier to ensure this sample completes before next
                D3D12_RESOURCE_BARRIER uavBarrier = {};
                uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                uavBarrier.UAV.pResource = m_outputTexture.Get();
                renderCommandList->ResourceBarrier(1, &uavBarrier);
                
                // Execute GPU work periodically to allow progress updates
                bool isLastSample = (sampleIdx == samplesPerPixel - 1);
                bool shouldExecute = ((sampleIdx + 1) % batchSize == 0) || isLastSample;
                
                if (shouldExecute) {
                    // Close and execute command list
                    ThrowIfFailed(renderCommandList->Close());
                    ID3D12CommandList* lists[] = { renderCommandList.Get() };
                    m_commandQueue->ExecuteCommandLists(1, lists);
                    
                    // Signal and wait for GPU
                    const UINT64 currentFence = m_offlineFenceValue;
                    ThrowIfFailed(m_commandQueue->Signal(m_offlineFence.Get(), currentFence));
                    m_offlineFenceValue++;
                    
                    if (m_offlineFence->GetCompletedValue() < currentFence) {
                        ThrowIfFailed(m_offlineFence->SetEventOnCompletion(currentFence, m_offlineFenceEvent));
                        WaitForSingleObject(m_offlineFenceEvent, INFINITE);
                    }
                    
                    // Update progress counter AFTER GPU completes
                    m_accumulatedSamples = sampleIdx + 1;
                    
                    // If not last sample, reset command list for next batch
                    if (!isLastSample) {
                        ThrowIfFailed(m_offlineCommandAllocator->Reset());
                        ThrowIfFailed(renderCommandList->Reset(m_offlineCommandAllocator.Get(), nullptr));
                        
                        // Re-setup pipeline state
                        renderCommandList->SetPipelineState1(m_dxrStateObject.Get());
                        renderCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
                        
                        // Set descriptor heaps
                        ID3D12DescriptorHeap* descriptorHeaps[] = { m_srvUavHeap.Get() };
                        renderCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
                        
                        // Re-bind all resources
                        // Root parameter 0: Output UAV
                        D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        uavHandle.ptr += m_uavIndex_Output * m_srvUavDescriptorSize;
                        renderCommandList->SetComputeRootDescriptorTable(0, uavHandle);
                        
                        // Root parameter 1: TLAS
                        renderCommandList->SetComputeRootShaderResourceView(1, m_topLevelAS->GetGPUVirtualAddress());
                        
                        // Root parameter 2: Vertices SRV table
                        D3D12_GPU_DESCRIPTOR_HANDLE verticesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        verticesHandle.ptr += m_srvIndex_Vertices * m_srvUavDescriptorSize;
                        renderCommandList->SetComputeRootDescriptorTable(2, verticesHandle);
                        
                        // Root parameter 3: Indices SRV table
                        D3D12_GPU_DESCRIPTOR_HANDLE indicesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        indicesHandle.ptr += m_srvIndex_Indices * m_srvUavDescriptorSize;
                        renderCommandList->SetComputeRootDescriptorTable(3, indicesHandle);
                        
                        // Root parameter 4: Triangle materials SRV table
                        D3D12_GPU_DESCRIPTOR_HANDLE triMatHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        triMatHandle.ptr += 3 * m_srvUavDescriptorSize;
                        renderCommandList->SetComputeRootDescriptorTable(4, triMatHandle);
                        
                        // Root parameter 5: Materials SRV
                        D3D12_GPU_VIRTUAL_ADDRESS materialsAddress = m_materialBuffer->GetGPUVirtualAddress();
                        renderCommandList->SetComputeRootShaderResourceView(5, materialsAddress);
                        
                        // Root parameter 10: Material Layers SRV table
                        D3D12_GPU_DESCRIPTOR_HANDLE layersHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        layersHandle.ptr += m_srvIndex_MaterialLayers * m_srvUavDescriptorSize;
                        renderCommandList->SetComputeRootDescriptorTable(10, layersHandle);
                        
                        // Root parameter 6: Textures SRV table (t3, bound to descriptor slot 5)
                        D3D12_GPU_DESCRIPTOR_HANDLE texturesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        texturesHandle.ptr += 5 * m_srvUavDescriptorSize; // slot 5 for textures
                        renderCommandList->SetComputeRootDescriptorTable(6, texturesHandle);
                        
                        // Root parameter 7: Environment map SRV table (t4, bind to descriptor slot 6)
                        D3D12_GPU_DESCRIPTOR_HANDLE envMapHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        envMapHandle.ptr += 6 * m_srvUavDescriptorSize; // slot 6 for environment map
                        renderCommandList->SetComputeRootDescriptorTable(7, envMapHandle);
                        
                        // Root parameter 8: Virtual Texture Cache SRV table (t5, bind to descriptor slot 7)
                        if (m_useVirtualTextures) {
                            D3D12_GPU_DESCRIPTOR_HANDLE vtCacheHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                            vtCacheHandle.ptr += 7 * m_srvUavDescriptorSize; // slot 7 for virtual texture cache
                            renderCommandList->SetComputeRootDescriptorTable(8, vtCacheHandle);
                        }
                        
                        // Root parameter 9: Indirection Texture SRV table (t6, bind to descriptor slot 8)
                        if (m_useVirtualTextures) {
                            D3D12_GPU_DESCRIPTOR_HANDLE indirectionHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                            indirectionHandle.ptr += 8 * m_srvUavDescriptorSize; // slot 8 for indirection texture
                            renderCommandList->SetComputeRootDescriptorTable(9, indirectionHandle);
                        }
                        
                        // Root parameter 11: Texture Scales SRV table (t8, bind to descriptor slot 9)
                        D3D12_GPU_DESCRIPTOR_HANDLE scalesHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
                        scalesHandle.ptr += 9 * m_srvUavDescriptorSize; // slot 9 for texture scales
                        renderCommandList->SetComputeRootDescriptorTable(11, scalesHandle);
                        
                        // Root parameter 12: Camera constants (need to update frameIndex for next batch)
                        // Note: Next batch starts at sampleIdx + 1
                        CameraConstants nextCameraConstants;
                        nextCameraConstants.viewInverse = cameraToWorld;
                        nextCameraConstants.projInverse = projInverse;
                        nextCameraConstants.frameIndex = static_cast<uint32_t>(sampleIdx + 1); // Next sample index
                        nextCameraConstants.maxBounces = static_cast<uint32_t>(maxBounces);
                        nextCameraConstants.environmentLightIntensity = m_environmentLightIntensity;
                        nextCameraConstants.padding = 0.0f;
                        nextCameraConstants.cameraParams = glm::vec4(
                            m_camera.GetFOV(),
                            static_cast<float>(m_width) / static_cast<float>(m_height),
                            m_camera.GetAperture(),
                            m_camera.GetFocusDistance()
                        );
                        nextCameraConstants.sunDirIntensity = glm::vec4(m_sunDirection, m_sunIntensity);
                        nextCameraConstants.sunColorEnabled = glm::vec4(m_sunColor, 1.0f);  // Always 1.0, controlled by intensity
                        
                        renderCommandList->SetComputeRoot32BitConstants(12, sizeof(CameraConstants) / 4, &nextCameraConstants, 0);
                        
                        PIXBeginEvent(renderCommandList.Get(), PIX_COLOR_INDEX(1), "Path Tracing Loop");
                    }
                }
            }
            
            PIXEndEvent(renderCommandList.Get());  // End "Path Tracing Loop"
            
            std::cout << "All samples dispatched successfully" << std::endl;
            std::cout.flush();

            // Create new command list for readback operations
            ThrowIfFailed(m_offlineCommandAllocator->Reset());
            ThrowIfFailed(renderCommandList->Reset(m_offlineCommandAllocator.Get(), nullptr));

            // Transition output texture to copy source
            std::cout << "Transitioning output texture..." << std::endl;
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_outputTexture.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE
            );
            renderCommandList->ResourceBarrier(1, &barrier);

            // Create readback buffer
            std::cout << "Creating readback buffer..." << std::endl;
            D3D12_RESOURCE_DESC textureDesc = m_outputTexture->GetDesc();
            UINT64 textureSize;
            m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &textureSize);
            
            std::cout << "Texture size for readback: " << textureSize << " bytes" << std::endl;
            std::cout << "Texture format: " << textureDesc.Format 
                      << ", Width: " << textureDesc.Width 
                      << ", Height: " << textureDesc.Height << std::endl;

            Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
            HRESULT hrReadback = m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(textureSize),
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readbackBuffer));
            
            if (FAILED(hrReadback)) {
                char errorMsg[256];
                sprintf_s(errorMsg, "Failed to create readback buffer (HRESULT: 0x%08X, size: %llu bytes)", hrReadback, textureSize);
                ThrowIfFailed(hrReadback, errorMsg);
            }

            // Copy texture to readback buffer
            std::cout << "Copying to readback buffer..." << std::endl;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
            m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);
        
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readbackBuffer.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_outputTexture.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

            renderCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            // Transition back to UAV
            std::cout << "Transitioning back to UAV..." << std::endl;
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_outputTexture.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            );
            renderCommandList->ResourceBarrier(1, &barrier);

            // Execute readback command list
            std::cout << "Closing command list..." << std::endl;
            hr = renderCommandList->Close();
            if (FAILED(hr)) {
                char errMsg[256];
                sprintf_s(errMsg, "Failed to close render command list (HRESULT: 0x%08X)", hr);
                throw std::runtime_error(errMsg);
            }
            
            std::cout << "Executing readback command list..." << std::endl;
            ID3D12CommandList* lists[] = { renderCommandList.Get() };
            m_commandQueue->ExecuteCommandLists(1, lists);
            
            // Wait for readback to complete
            std::cout << "Waiting for GPU readback..." << std::endl;
            std::cout.flush();
            
            const UINT64 fence = m_offlineFenceValue;
            hr = m_commandQueue->Signal(m_offlineFence.Get(), fence);
            if (FAILED(hr)) {
                char errMsg[256];
                sprintf_s(errMsg, "Failed to signal fence (HRESULT: 0x%08X)", hr);
                throw std::runtime_error(errMsg);
            }
            std::cout << "Fence signaled: " << fence << std::endl;
            std::cout.flush();
            m_offlineFenceValue++;
            
            // Check current fence value
            UINT64 currentValue = m_offlineFence->GetCompletedValue();
            std::cout << "Current fence value: " << currentValue << ", waiting for: " << fence << std::endl;
            std::cout.flush();
            
            if (currentValue < fence) {
                std::cout << "GPU not done yet, setting event..." << std::endl;
                std::cout.flush();
                
                hr = m_offlineFence->SetEventOnCompletion(fence, m_offlineFenceEvent);
                if (FAILED(hr)) {
                    char errMsg[256];
                    sprintf_s(errMsg, "Failed to set fence event (HRESULT: 0x%08X)", hr);
                    throw std::runtime_error(errMsg);
                }
                
                std::cout << "Waiting for fence event..." << std::endl;
                std::cout.flush();
                
                // Wait with timeout (5 seconds instead of 10)
                DWORD waitResult = WaitForSingleObject(m_offlineFenceEvent, 5000);
                if (waitResult == WAIT_TIMEOUT) {
                    std::cerr << "ERROR: GPU wait timeout after 5 seconds!" << std::endl;
                    std::cerr << "Current fence value: " << m_offlineFence->GetCompletedValue() << std::endl;
                    std::cerr.flush();
                    // Force cleanup and throw
                    if (readbackBuffer) {
                        readbackBuffer->Release();
                        readbackBuffer = nullptr;
                    }
                    throw std::runtime_error("GPU timeout - render operation took too long");
                } else if (waitResult == WAIT_OBJECT_0) {
                    std::cout << "GPU work completed successfully" << std::endl;
                    std::cout.flush();
                } else {
                    std::cerr << "ERROR: WaitForSingleObject failed with result: " << waitResult << std::endl;
                    std::cerr.flush();
                    throw std::runtime_error("GPU wait failed unexpectedly");
                }
            } else {
                std::cout << "GPU already completed" << std::endl;
                std::cout.flush();
            }

            // Read back data and save to PPM
            std::cout << "Reading back data..." << std::endl;
            void* mappedData;
            ThrowIfFailed(readbackBuffer->Map(0, nullptr, &mappedData), "Failed to map readback buffer");

            const float* pixels = static_cast<const float*>(mappedData); // Now reading float data

            // 准备降噪数据
            std::vector<float> inputImage(m_width * m_height * 3);
            std::vector<float> denoisedImage(m_width * m_height * 3);
            float invSamples = 1.0f / static_cast<float>(samplesPerPixel);

            // 从GPU数据复制到输入图像（RGBA -> RGB，并平均采样）
            std::cout << "Preparing image for denoising..." << std::endl;
            for (UINT y = 0; y < m_height; ++y) {
                const float* row = reinterpret_cast<const float*>(
                    reinterpret_cast<const uint8_t*>(pixels) + y * footprint.Footprint.RowPitch
                );
                for (UINT x = 0; x < m_width; ++x) {
                    const float* pixel = row + x * 4; // RGBA
                    size_t idx = (y * m_width + x) * 3;
                    inputImage[idx + 0] = pixel[0] * invSamples; // R
                    inputImage[idx + 1] = pixel[1] * invSamples; // G
                    inputImage[idx + 2] = pixel[2] * invSamples; // B
                }
            }

            // 执行降噪
            bool denoised = false;
            if (m_denoiser && m_denoiser->IsInitialized()) {
                std::cout << "Denoising image..." << std::endl;
                denoised = m_denoiser->Denoise(
                    inputImage.data(),
                    denoisedImage.data(),
                    m_width,
                    m_height
                );
                if (!denoised) {
                    std::cerr << "Denoising failed: " << m_denoiser->GetError() << std::endl;
                    std::cout << "Saving original (non-denoised) image" << std::endl;
                }
            } else {
                std::cout << "Denoiser not available, saving original image" << std::endl;
            }

            // 使用降噪后的图像（如果降噪成功）或原始图像
            const float* finalImage = denoised ? denoisedImage.data() : inputImage.data();

            // Write PPM file
            std::cout << "Writing PPM file..." << std::endl;
            std::ofstream file(outputPath, std::ios::binary);
            if (!file.is_open()) {
                readbackBuffer->Unmap(0, nullptr);
                throw std::runtime_error("Failed to open output file: " + outputPath);
            }

            file << "P6\n" << m_width << " " << m_height << "\n255\n";

            // 将float图像转换为8位并写入
            for (UINT y = 0; y < m_height; ++y) {
                for (UINT x = 0; x < m_width; ++x) {
                    size_t idx = (y * m_width + x) * 3;
                    float r = finalImage[idx + 0];
                    float g = finalImage[idx + 1];
                    float b = finalImage[idx + 2];
                    
                    // Clamp and convert to 8-bit
                    uint8_t rgb[3];
                    rgb[0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * 255.0f)));
                    rgb[1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * 255.0f)));
                    rgb[2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * 255.0f)));
                    
                    file.write(reinterpret_cast<const char*>(rgb), 3);
                }
            }

            file.close();
            readbackBuffer->Unmap(0, nullptr);

            std::cout << "Render complete: " << outputPath << std::endl;
        }
        catch (const com_exception& e) {
            char errMsg[512];
            sprintf_s(errMsg, "RenderToFile DirectX error (HRESULT: 0x%08X): %s", e.get_result(), e.what());
            std::cerr << errMsg << std::endl;
            throw std::runtime_error(errMsg);
        }
        catch (const std::exception& e) {
            std::string errMsg = std::string("RenderToFile exception: ") + (e.what() ? e.what() : "Unknown error");
            std::cerr << errMsg << std::endl;
            throw std::runtime_error(errMsg);
        }
        catch (...) {
            std::cerr << "RenderToFile: Unknown exception caught" << std::endl;
            throw std::runtime_error("Unknown error in RenderToFile");
        }
    }


    void Renderer::InitPipeline(HWND hwnd) {
        CreateDevice();
        CreateCommandQueueAndList();
        CreateSwapChain(hwnd);
        CreateDescriptorHeaps();
        CreateRenderTargets();

        // Create synchronization objects
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        std::cout << "DX12 pipeline initialized successfully" << std::endl;
    }

    void Renderer::CreateDevice() {
#ifdef _DEBUG
        // Enable the D3D12 debug layer
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            std::cout << "D3D12 Debug Layer enabled" << std::endl;
        }
#endif
        m_adapter = GetAdapter(false);
        ThrowIfFailed(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
    }

    void Renderer::CreateCommandQueueAndList() {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

        // Create one command allocator per frame to avoid conflicts
        for (UINT i = 0; i < FrameCount; i++) {
            ThrowIfFailed(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, 
                IID_PPV_ARGS(&m_commandAllocators[i])));
        }

        // Create command list using first allocator
        ThrowIfFailed(m_device->CreateCommandList(
            0, 
            D3D12_COMMAND_LIST_TYPE_DIRECT, 
            m_commandAllocators[0].Get(), 
            nullptr, 
            IID_PPV_ARGS(&m_commandList)));
        ThrowIfFailed(m_commandList->Close());
    }

    void Renderer::CreateSwapChain(HWND hwnd) {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain
        ));

        ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain.As(&m_swapChain));
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        std::cout << "SwapChain created successfully" << std::endl;
    }

    void Renderer::CreateDescriptorHeaps() {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create SRV/UAV heap for DXR (raytracing resources)
        D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc = {};
        srvUavHeapDesc.NumDescriptors = 15; // UAV(output) + SRV(TLAS, vertices, indices, triangleMaterials, materials, materialLayers, textures, environment map, virtual texture cache, indirection texture)
        srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvUavHeapDesc, IID_PPV_ARGS(&m_srvUavHeap)));
        
        // Create separate SRV heap for ImGui (to avoid conflicts with offline rendering)
        D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
        imguiHeapDesc.NumDescriptors = 1; // ImGui only needs 1 descriptor for font texture
        imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiSrvHeap)));

        std::cout << "Descriptor heaps created successfully" << std::endl;
    }

    void Renderer::CreateRenderTargets() {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < FrameCount; i++) {
            ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;
        }
    }

    void Renderer::CreateRaytracingPipeline() {
        // Check for DXR support first
        if (!m_dxrSupported) {
            return;
        }

        try {
            std::cout << "Compiling shader library..." << std::endl;
            // Compile shader library
            m_raytracingShaderLibrary = CompileShader(L"shaders/Raytracing.hlsl");
            
            if (!m_raytracingShaderLibrary) {
                throw std::runtime_error("Shader compilation returned null");
            }
            std::cout << "Shader compiled successfully (" << m_raytracingShaderLibrary->GetBufferSize() << " bytes)" << std::endl;
            
            // Create root signature
            std::cout << "Creating root signature..." << std::endl;
            CreateRaytracingRootSignature();
            
            // Create DXR pipeline state object
            CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

            // 1. DXIL Library
            auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            D3D12_SHADER_BYTECODE libdxil = {};
            libdxil.BytecodeLength = m_raytracingShaderLibrary->GetBufferSize();
            libdxil.pShaderBytecode = m_raytracingShaderLibrary->GetBufferPointer();
            lib->SetDXILLibrary(&libdxil);
            const WCHAR* shaderExports[] = { L"RayGen", L"Miss", L"ClosestHit" };
            lib->DefineExports(shaderExports);

            // 2. Hit Group
            auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
            hitGroup->SetClosestHitShaderImport(L"ClosestHit");
            hitGroup->SetHitGroupExport(L"HitGroup");
            hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

            // 3. Shader Config
            auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
            // RadiancePayload layout in HLSL:
            //   float3 radiance;      // 12
            //   float3 throughput;    // 12
            //   float3 nextOrigin;    // 12
            //   float3 nextDirection; // 12
            //   uint   rngState;      // 4
            //   bool   terminated;    // 4 (HLSL bool is 4 bytes)
            //   float  iorStack[4];   // 16
            //   uint   iorStackTop;   // 4
            // Total = 76 bytes
            UINT payloadSize = (4 * 3 * sizeof(float)) + (2 * sizeof(UINT)) + (4 * sizeof(float)) + sizeof(UINT);
            UINT attributeSize = 2 * sizeof(float); // BuiltInTriangleIntersectionAttributes: float2 barycentrics
            shaderConfig->Config(payloadSize, attributeSize);

            // 4. Global Root Signature
            auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

            // 7. Pipeline Config
            auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
            UINT maxRecursionDepth = 1; // No recursion - iterative path tracing in RayGen
            pipelineConfig->Config(maxRecursionDepth);

            std::cout << "Creating DXR state object..." << std::endl;
            
            // Create the state object
            HRESULT hr = m_device->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject));
            if (FAILED(hr)) {
                char errorMsg[256];
                sprintf_s(errorMsg, "Failed to create DXR state object (HRESULT: 0x%08X)", hr);
                throw std::runtime_error(errorMsg);
            }
                
            std::cout << "DXR state object created successfully" << std::endl;
            std::cout << "DXR pipeline created successfully" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to create raytracing pipeline: " << e.what() << std::endl;
            m_dxrSupported = false;
        }
    }

    void Renderer::CheckRaytracingSupport() {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
        HRESULT hr = m_device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5,
            &features,
            sizeof(features)
        );

        if (FAILED(hr) || features.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            m_dxrSupported = false;
            std::cerr << "DirectX Raytracing is not supported on this device" << std::endl;
            return;
        }

        m_dxrSupported = true;
        std::cout << "DirectX Raytracing supported (Tier ";
        switch (features.RaytracingTier) {
            case D3D12_RAYTRACING_TIER_1_0:
                std::cout << "1.0";
                break;
            case D3D12_RAYTRACING_TIER_1_1:
                std::cout << "1.1";
                break;
            default:
                std::cout << "Unknown";
                break;
        }
        std::cout << ")" << std::endl;
    }

    Microsoft::WRL::ComPtr<IDxcBlob> Renderer::CompileShader(const std::wstring& filename) {
        // Initialize DXC compiler
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
        ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));

        // Create default include handler
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
        ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler));

        // Load shader source
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
        ThrowIfFailed(utils->LoadFile(filename.c_str(), nullptr, &sourceBlob));

        // Compile arguments
        std::vector<LPCWSTR> arguments = {
            filename.c_str(),
            L"-E", L"",  // No entry point for library
            L"-T", L"lib_6_6",  // Shader model 6.6 for GeometryIndex() support
            L"-I", L"shaders",  // Include directory
            L"-HV", L"2021",
#ifdef _DEBUG
            L"-Zi",
            L"-Od",
#else
            L"-O3",
#endif
        };

        DxcBuffer sourceBuffer = {};
        sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
        sourceBuffer.Size = sourceBlob->GetBufferSize();
        sourceBuffer.Encoding = 0;

        Microsoft::WRL::ComPtr<IDxcResult> result;
        ThrowIfFailed(compiler->Compile(
            &sourceBuffer,
            arguments.data(),
            (UINT32)arguments.size(),
            includeHandler.Get(),
            IID_PPV_ARGS(&result)
        ));

        // Check for errors
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            std::cerr << "Shader compilation warnings/errors:\n" << errors->GetStringPointer() << std::endl;
        }

        HRESULT hrStatus;
        result->GetStatus(&hrStatus);
        ThrowIfFailed(hrStatus, "Shader compilation failed");

        Microsoft::WRL::ComPtr<IDxcBlob> shader;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr);
        
        std::wcout << L"Shader compiled successfully: " << filename << std::endl;
        return shader;
    }

    void Renderer::CreateRaytracingRootSignature() {
        // Create a root signature with global resources
        CD3DX12_DESCRIPTOR_RANGE1 ranges[12];  // Extended for Texture Scales
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0: output texture
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0: acceleration structure
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); // t1 space0: vertices
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 1); // t1 space1: indices
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 2); // t1 space2: triangle material indices
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2: materials
        ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // t3: textures (texture array)
        ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // t4: environment map
        ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5); // t5: virtual texture cache
        ranges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6); // t6: indirection texture
        ranges[10].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7); // t7: material layers
        ranges[11].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8); // t8: texture scales

        // Static sampler for texture sampling (with WRAP address mode for tiling)
        CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
            0,                                      // register(s0)
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,       // filter
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,       // addressU
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,       // addressV
            D3D12_TEXTURE_ADDRESS_MODE_WRAP);      // addressW

        CD3DX12_ROOT_PARAMETER1 rootParameters[13];  // Extended for Texture Scales
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0]); // Output UAV
        rootParameters[1].InitAsShaderResourceView(0); // Acceleration structure (SRV)
        rootParameters[2].InitAsDescriptorTable(1, &ranges[2]); // Vertices (t1, space0)
        rootParameters[3].InitAsDescriptorTable(1, &ranges[3]); // Indices (t1, space1)
        rootParameters[4].InitAsDescriptorTable(1, &ranges[4]); // Triangle material indices (t1, space2)
        rootParameters[5].InitAsShaderResourceView(2); // Materials (t2) - ROOT DESCRIPTOR
        rootParameters[6].InitAsDescriptorTable(1, &ranges[6]); // Textures (t3)
        rootParameters[7].InitAsDescriptorTable(1, &ranges[7]); // Environment map (t4)
        rootParameters[8].InitAsDescriptorTable(1, &ranges[8]); // Virtual texture cache (t5)
        rootParameters[9].InitAsDescriptorTable(1, &ranges[9]); // Indirection texture (t6)
        rootParameters[10].InitAsDescriptorTable(1, &ranges[10]); // Material layers (t7)
        rootParameters[11].InitAsDescriptorTable(1, &ranges[11]); // Texture scales (t8)
        // Scene constants (b0): view and projection matrices
        rootParameters[12].InitAsConstants(sizeof(CameraConstants) / 4, 0);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &samplerDesc,
            D3D12_ROOT_SIGNATURE_FLAG_NONE);

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error),
            "Failed to serialize root signature");
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&m_raytracingGlobalRootSignature)),
            "Failed to create root signature");
            
        std::cout << "Root signature created (with Material Layers + Virtual Texture support)" << std::endl;
    }

// Sun setter implementations (moved from header for logging)
void ACG::Renderer::SetSunDirection(const glm::vec3& dir) {
    m_sunDirection = glm::normalize(dir);
}

void ACG::Renderer::SetSunColor(const glm::vec3& color) {
    m_sunColor = color;
}

void ACG::Renderer::SetSunIntensity(float intensity) {
    m_sunIntensity = intensity;
}

    void Renderer::CreateAccelerationStructures(ID3D12GraphicsCommandList4* cmdList) {
        if (!m_dxrSupported) {
            std::cout << "Skipping AS creation: DXR not supported" << std::endl;
            return;
        }
        if (!m_scene) {
            std::cout << "Skipping AS creation: Scene not loaded" << std::endl;
            return;
        }
        if (!m_vertexBuffer) {
            std::cout << "Skipping AS creation: Vertex buffer not ready" << std::endl;
            return;
        }
        if (!m_indexBuffer) {
            std::cout << "Skipping AS creation: Index buffer not ready" << std::endl;
            return;
        }

        std::cout << "Building acceleration structures..." << std::endl;
        
        // CRITICAL: Ensure vertex and index data are fully uploaded before building AS
        // The buffers were transitioned to GENERIC_READ in CreateDefaultBuffer
        // Add UAV barrier to ensure all copy operations complete before AS build
        D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
        copyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        copyBarriers[0].UAV.pResource = nullptr;  // Global UAV barrier
        copyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        copyBarriers[1].UAV.pResource = nullptr;
        cmdList->ResourceBarrier(1, &copyBarriers[0]);
        
        // INDUSTRY STANDARD: Single geometry descriptor with unified vertex/index buffers
        // This is the recommended approach for static scenes in production engines
        // (Unity, Unreal, and most ray tracers use this method)
        
        // Count total triangles and vertices
        UINT totalTriangles = 0;
        UINT totalVertices = 0;
        for (const auto& mesh : m_scene->GetMeshes()) {
            totalTriangles += static_cast<UINT>(mesh->GetIndices().size() / 3);
            totalVertices += static_cast<UINT>(mesh->GetVertices().size());
        }

        std::cout << "  Creating single unified geometry descriptor:" << std::endl;
        std::cout << "    Total: " << totalTriangles << " triangles, " 
                  << totalVertices << " vertices" << std::endl;
        
        // Single geometry descriptor encompassing all meshes
        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        
        // Unified vertex buffer (all meshes, global indices)
        geometryDesc.Triangles.Transform3x4 = 0;
        geometryDesc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = 48;
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.VertexCount = totalVertices;
        
        // Unified index buffer (global indices, already offset by baseVertex)
        geometryDesc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.IndexCount = totalTriangles * 3;

        try {
            // Build Bottom Level AS (BLAS) with single geometry
            
            // Get required sizes for BLAS
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs = {};
            blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            blasInputs.NumDescs = 1;  // Single unified geometry
            blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            blasInputs.pGeometryDescs = &geometryDesc;

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasPrebuildInfo = {};
            m_device->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasPrebuildInfo);

            // Align scratch buffer size
            UINT64 scratchBufferSize = (blasPrebuildInfo.ScratchDataSizeInBytes + 255) & ~255;
            UINT64 blasBufferSize = (blasPrebuildInfo.ResultDataMaxSizeInBytes + 255) & ~255;

            // Create scratch buffer for BLAS build (save as member to keep alive)
            HRESULT hr = m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(scratchBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_blasScratchBuffer));
            if (FAILED(hr)) {
                char errorMsg[512];
                if (hr == DXGI_ERROR_DEVICE_REMOVED) {
                    HRESULT removedReason = m_device->GetDeviceRemovedReason();
                    sprintf_s(errorMsg, "Failed to create BLAS scratch buffer - Device Removed (HRESULT: 0x%08X, Removed Reason: 0x%08X)", hr, removedReason);
                } else {
                    sprintf_s(errorMsg, "Failed to create BLAS scratch buffer (HRESULT: 0x%08X)", hr);
                }
                std::cerr << errorMsg << std::endl;
                throw std::runtime_error(errorMsg);
            }

            // Create BLAS buffer
            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(blasBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                IID_PPV_ARGS(&m_bottomLevelAS)), 
                "Failed to create BLAS buffer");

            // Build BLAS
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
        blasDesc.Inputs = blasInputs;
        blasDesc.ScratchAccelerationStructureData = m_blasScratchBuffer->GetGPUVirtualAddress();
        blasDesc.DestAccelerationStructureData = m_bottomLevelAS->GetGPUVirtualAddress();

        cmdList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

        // UAV barrier for BLAS
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = m_bottomLevelAS.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        // Build Top Level AS (TLAS)
        D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
        // Identity transform (3x4 matrix in row-major)
        // Initialize ALL elements to avoid garbage values
        memset(&instanceDesc.Transform, 0, sizeof(instanceDesc.Transform));
        instanceDesc.Transform[0][0] = 1.0f;
        instanceDesc.Transform[1][1] = 1.0f;
        instanceDesc.Transform[2][2] = 1.0f;
        instanceDesc.InstanceID = 0;
        instanceDesc.InstanceMask = 0xFF;
        instanceDesc.InstanceContributionToHitGroupIndex = 0;
        instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        instanceDesc.AccelerationStructure = m_bottomLevelAS->GetGPUVirtualAddress();

        // Upload instance descriptor (save as member to keep alive)
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC)),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_instanceDescBuffer)));

        void* mappedData;
        m_instanceDescBuffer->Map(0, nullptr, &mappedData);
        memcpy(mappedData, &instanceDesc, sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
        m_instanceDescBuffer->Unmap(0, nullptr);

        // Get TLAS prebuild info
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
        tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        tlasInputs.NumDescs = 1;
        tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        tlasInputs.InstanceDescs = m_instanceDescBuffer->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo = {};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasPrebuildInfo);

        UINT64 tlasScratchSize = (tlasPrebuildInfo.ScratchDataSizeInBytes + 255) & ~255;
        UINT64 tlasBufferSize = (tlasPrebuildInfo.ResultDataMaxSizeInBytes + 255) & ~255;

        // Create TLAS scratch buffer (save as member to keep alive)
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(tlasScratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_tlasScratchBuffer)));

        // Create TLAS buffer
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(tlasBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr,
            IID_PPV_ARGS(&m_topLevelAS)));

        // Build TLAS
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
        tlasDesc.Inputs = tlasInputs;
        tlasDesc.ScratchAccelerationStructureData = m_tlasScratchBuffer->GetGPUVirtualAddress();
        tlasDesc.DestAccelerationStructureData = m_topLevelAS->GetGPUVirtualAddress();

        cmdList->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);

        // UAV barrier for TLAS
        uavBarrier.UAV.pResource = m_topLevelAS.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        // Create SRV for TLAS (descriptor index 4)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvTLASDesc = {};
        srvTLASDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvTLASDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvTLASDesc.RaytracingAccelerationStructure.Location = m_topLevelAS->GetGPUVirtualAddress();
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE tlasSrvHandle = { srvHandle.ptr + descriptorSize * 4 };
        m_device->CreateShaderResourceView(nullptr, &srvTLASDesc, tlasSrvHandle);

        // Note: Do NOT close or execute here - caller will handle command list execution

        std::cout << "Acceleration structures built successfully: "
                  << totalTriangles << " triangles, "
                  << totalVertices << " vertices (single unified geometry)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create acceleration structures: " << e.what() << std::endl;
            throw;
        }
    }

    void Renderer::CreateShaderResources(ID3D12GraphicsCommandList4* cmdList) {
        // Create output texture, vertex/index/material buffers and upload to GPU
        if (!m_scene) return;

        // Note: Command list should be ready (opened) by caller
        // Do NOT reset allocator here - it may be in use

        // Flatten vertices and indices from all meshes into single buffers
        struct GPUVertex { 
            float position[3]; 
            float normal[3]; 
            float texCoord[2]; 
            float tangent[3];  // Must match HLSL Vertex struct
            float _pad;        // Padding to align to 16 bytes (44 -> 48)
        };
        std::vector<GPUVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> triangleMaterialIndices; // Material index per triangle

        for (const auto& mesh : m_scene->GetMeshes()) {
            const auto& meshVerts = mesh->GetVertices();
            const auto& meshIdx = mesh->GetIndices();
            uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            int meshMaterialIdx = mesh->GetMaterialIndex();
            std::cout << "Mesh: " << meshVerts.size() << " vertices, " << meshIdx.size()/3 << " triangles, materialIndex=" << meshMaterialIdx << std::endl;
            
            // append vertices
            for (const auto& v : meshVerts) {
                GPUVertex outV = {};
                outV.position[0] = v.position.x; outV.position[1] = v.position.y; outV.position[2] = v.position.z;
                outV.normal[0] = v.normal.x; outV.normal[1] = v.normal.y; outV.normal[2] = v.normal.z;
                outV.texCoord[0] = v.texCoord.x; outV.texCoord[1] = v.texCoord.y;
                outV.tangent[0] = 0.0f; outV.tangent[1] = 0.0f; outV.tangent[2] = 0.0f;
                outV._pad = 0.0f;
                vertices.push_back(outV);
            }
            // append indices with base offset
            for (uint32_t idx : meshIdx) {
                indices.push_back(baseVertex + idx);
            }
            // Store material index for each triangle in this mesh
            uint32_t numTriangles = static_cast<uint32_t>(meshIdx.size() / 3);
            for (uint32_t i = 0; i < numTriangles; i++) {
                triangleMaterialIndices.push_back(meshMaterialIdx);
            }
        }

        // Create GPU buffers using helper CreateDefaultBuffer
        // CRITICAL: Upload buffers MUST be kept alive until GPU executes the copy!
        size_t vertexBufferSize = sizeof(GPUVertex) * vertices.size();
        if (vertexBufferSize > 0) {
            m_vertexBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, vertices.data(), vertexBufferSize, m_vertexUpload);
            std::cout << "Vertex buffer created: " << vertices.size() << " vertices (" << vertexBufferSize << " bytes)" << std::endl;
        }

        size_t indexBufferSize = sizeof(uint32_t) * indices.size();
        if (indexBufferSize > 0) {
            m_indexBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, indices.data(), indexBufferSize, m_indexUpload);
            std::cout << "Index buffer created: " << indices.size() << " indices (" << indexBufferSize << " bytes)" << std::endl;
        }

        // Create triangle material index buffer (one material ID per triangle)
        size_t triangleMaterialBufferSize = sizeof(uint32_t) * triangleMaterialIndices.size();
        if (triangleMaterialBufferSize > 0) {
            m_triangleMaterialBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, 
                triangleMaterialIndices.data(), triangleMaterialBufferSize, m_triangleMaterialUpload);
            std::cout << "Triangle material buffer created: " << triangleMaterialIndices.size() << " triangles" << std::endl;
        }

        // Create GPU-side material buffer
        std::vector<MaterialData> materialsCPU;
        std::vector<std::shared_ptr<Texture>> textures;  // Collect textures
        std::unordered_map<Material*, int> materialTextureIndex;  // Map material to texture index
        
        if (m_scene->GetMaterials().empty()) {
            // Add a default material if none are loaded
            MaterialData mat;
            mat.baseColor_metallic = glm::vec4(0.8f, 0.8f, 0.8f, 0.0f);  // gray diffuse
            mat.emission_roughness = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);  // no emission, medium roughness
            mat.ior_opacity_flags_idx = glm::vec4(1.5f, 1.0f, 0.0f, 0.0f);  // IOR=1.5, opaque
            mat.texIndices = glm::vec4(0.0f);  // no textures
            materialsCPU.push_back(mat);
        } else {
            // Collect unique textures from materials
            std::map<Texture*, int> textureIndexMap;
            
            for (const auto& m : m_scene->GetMaterials()) {
                // Collect all texture types
                auto baseColorTex = m->GetBaseColorTexture();
                if (baseColorTex && baseColorTex->GetWidth() > 0 && textureIndexMap.find(baseColorTex.get()) == textureIndexMap.end()) {
                    textureIndexMap[baseColorTex.get()] = textures.size();
                    textures.push_back(baseColorTex);
                }
                
                auto normalTex = m->GetNormalTexture();
                if (normalTex && normalTex->GetWidth() > 0 && textureIndexMap.find(normalTex.get()) == textureIndexMap.end()) {
                    textureIndexMap[normalTex.get()] = textures.size();
                    textures.push_back(normalTex);
                }
                
                auto metallicRoughnessTex = m->GetMetallicRoughnessTexture();
                if (metallicRoughnessTex && metallicRoughnessTex->GetWidth() > 0 && textureIndexMap.find(metallicRoughnessTex.get()) == textureIndexMap.end()) {
                    textureIndexMap[metallicRoughnessTex.get()] = textures.size();
                    textures.push_back(metallicRoughnessTex);
                }
                
                auto emissionTex = m->GetEmissionTexture();
                if (emissionTex && emissionTex->GetWidth() > 0 && textureIndexMap.find(emissionTex.get()) == textureIndexMap.end()) {
                    textureIndexMap[emissionTex.get()] = textures.size();
                    textures.push_back(emissionTex);
                }
            }
            
            std::cout << "Collected " << textures.size() << " unique textures from materials" << std::endl;

            // Create material data - use ToGPUData() which includes correct texture indices
            for (size_t i = 0; i < m_scene->GetMaterials().size(); ++i) {
                const auto& m = m_scene->GetMaterials()[i];
                MaterialData matData = m->ToGPUData();
                
                // Debug: print texture indices for all materials (show first 5 and any with textures)
                int32_t baseColorIdx, normalIdx, mrIdx, emissionIdx;
                std::memcpy(&baseColorIdx, &matData.texIndices.x, sizeof(int32_t));
                std::memcpy(&normalIdx, &matData.texIndices.y, sizeof(int32_t));
                std::memcpy(&mrIdx, &matData.texIndices.z, sizeof(int32_t));
                std::memcpy(&emissionIdx, &matData.texIndices.w, sizeof(int32_t));
                
                if (i < 5 || baseColorIdx >= 0 || normalIdx >= 0 || mrIdx >= 0 || emissionIdx >= 0) {
                    std::cout << "  Material " << i << " (" << m->GetName() << "): texIndices=["
                              << baseColorIdx << ", " << normalIdx << ", " << mrIdx << ", " << emissionIdx << "]";
                    if (m->GetBaseColorTexture()) {
                        std::cout << " [has baseColor tex: " << m->GetBaseColorTexIdx() << "]";
                    }
                    std::cout << std::endl;
                }
                
                materialsCPU.push_back(matData);
            }
        }

        // NOTE: Material buffer creation is DEFERRED until after texture system initialization
        // This allows us to adjust texture indices if using Virtual Textures

        // Upload textures to GPU (with batch processing for large texture sets)
        if (!textures.empty()) {
            std::cout << "Uploading " << textures.size() << " textures to GPU..." << std::endl;
            
            // **STEP 1: Pre-calculate total texture requirements**
            int totalTextures = static_cast<int>(textures.size());
            UINT maxWidth = 0;
            UINT maxHeight = 0;
            for (const auto& tex : textures) {
                maxWidth = std::max(maxWidth, static_cast<UINT>(tex->GetWidth()));
                maxHeight = std::max(maxHeight, static_cast<UINT>(tex->GetHeight()));
            }
            
            // Calculate memory requirements
            size_t arrayMemoryMB = (size_t)maxWidth * maxHeight * 4 * totalTextures / (1024 * 1024);
            std::cout << "  Total textures: " << totalTextures << std::endl;
            std::cout << "  Max dimensions: " << maxWidth << "x" << maxHeight << std::endl;
            std::cout << "  Estimated VRAM (Texture Array): " << arrayMemoryMB << " MB" << std::endl;
            
            // **DECISION POINT: Use Virtual Textures for very large texture sets**
            // Virtual Textures use DX12 Tiled Resources for on-demand streaming
            const size_t MAX_TEXTURE_ARRAY_VRAM_MB = 2048;  // 2GB limit for conventional arrays
            const UINT MAX_TEXTURE_DIMENSION = 2048;  // Max dimension per texture in array
            
            bool useVirtualTextures = false;
            
            if (arrayMemoryMB > MAX_TEXTURE_ARRAY_VRAM_MB) {
                std::cout << "  ⚠ Texture array would require " << arrayMemoryMB << " MB (exceeds " 
                          << MAX_TEXTURE_ARRAY_VRAM_MB << " MB limit)" << std::endl;
                std::cout << "  Attempting to use Virtual Texture System..." << std::endl;
                
                // Try to initialize Virtual Texture System
                VirtualTextureConfig vtConfig;
                vtConfig.tileSize = 256;
                vtConfig.maxPhysicalPages = 4096;  // 256MB physical memory
                vtConfig.maxVirtualTextures = 1024;
                
                if (m_virtualTextureSystem.Initialize(m_device.Get(), vtConfig)) {
                    std::cout << "  ✓ Virtual Texture System initialized successfully" << std::endl;
                    
                    // Add each texture to virtual texture system
                    bool allTexturesAdded = true;
                    for (int i = 0; i < totalTextures; ++i) {
                        int32_t vtIndex = m_virtualTextureSystem.AddVirtualTexture(textures[i]);
                        if (vtIndex < 0) {
                            std::cerr << "  ✗ ERROR: Failed to add texture " << i << " to virtual texture system" << std::endl;
                            allTexturesAdded = false;
                            break;
                        }
                    }
                    
                    if (allTexturesAdded) {
                        // Upload all tiles
                        if (m_virtualTextureSystem.UploadAllTiles(cmdList, m_commandQueue.Get())) {
                            // Create indirection texture
                            if (m_virtualTextureSystem.CreateIndirectionTexture(cmdList, m_commandQueue.Get())) {
                                // Set flag BEFORE creating SRVs so the function doesn't early-return
                                useVirtualTextures = true;
                                m_useVirtualTextures = true;
                                
                                // Create SRVs for Virtual Textures
                                CreateVirtualTextureSRVs();
                                
                                std::cout << "  ✓ Virtual Texture System ready" << std::endl;
                            } else {
                                std::cerr << "  ✗ Failed to create indirection texture" << std::endl;
                            }
                        } else {
                            std::cerr << "  ✗ Failed to upload tiles" << std::endl;
                        }
                    }
                }
                
                if (!useVirtualTextures) {
                    std::cerr << "  ✗ Virtual Texture initialization failed, falling back to downsampling" << std::endl;
                }
            }
            
            // **FALLBACK: Use conventional texture array with downsampling**
            if (!useVirtualTextures) {
                m_useVirtualTextures = false;
                
                // Apply aggressive downsampling if needed
                bool needsDownsampling = false;
                if (maxWidth > MAX_TEXTURE_DIMENSION || maxHeight > MAX_TEXTURE_DIMENSION) {
                    needsDownsampling = true;
                }
                if (arrayMemoryMB > MAX_TEXTURE_ARRAY_VRAM_MB) {
                    needsDownsampling = true;
                }
                
                if (needsDownsampling) {
                    float memoryRatio = static_cast<float>(MAX_TEXTURE_ARRAY_VRAM_MB) / static_cast<float>(arrayMemoryMB);
                    float dimensionScale = std::sqrt(std::min(memoryRatio, 1.0f));
                    
                    UINT targetMaxWidth = static_cast<UINT>(maxWidth * dimensionScale);
                    UINT targetMaxHeight = static_cast<UINT>(maxHeight * dimensionScale);
                    
                    targetMaxWidth = std::min(targetMaxWidth, MAX_TEXTURE_DIMENSION);
                    targetMaxHeight = std::min(targetMaxHeight, MAX_TEXTURE_DIMENSION);
                    targetMaxWidth = std::max(targetMaxWidth, 256U);
                    targetMaxHeight = std::max(targetMaxHeight, 256U);
                    
                    std::cout << "  Downsampling textures: " << maxWidth << "x" << maxHeight 
                              << " -> " << targetMaxWidth << "x" << targetMaxHeight << std::endl;
                    std::cout << "    Scale factor: " << (dimensionScale * 100.0f) << "%" << std::endl;
                    
                    maxWidth = targetMaxWidth;
                    maxHeight = targetMaxHeight;
                    
                    size_t newMemoryMB = (size_t)maxWidth * maxHeight * 4 * totalTextures / (1024 * 1024);
                    std::cout << "    New estimated VRAM: " << newMemoryMB << " MB" << std::endl;
                }
            }
            
            // **STEP 2: Create texture array resource**
            if (!useVirtualTextures) {
                if (m_textureAtlas == nullptr) {
                D3D12_RESOURCE_DESC texArrayDesc = {};
                texArrayDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                texArrayDesc.Width = maxWidth;
                texArrayDesc.Height = maxHeight;
                texArrayDesc.DepthOrArraySize = static_cast<UINT16>(totalTextures);
                texArrayDesc.MipLevels = 1;
                texArrayDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                texArrayDesc.SampleDesc.Count = 1;
                texArrayDesc.SampleDesc.Quality = 0;
                texArrayDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                texArrayDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
                
                CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
                ThrowIfFailed(m_device->CreateCommittedResource(
                    &defaultHeapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &texArrayDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&m_textureAtlas)
                ));
                m_textureAtlas->SetName(L"Texture Array");
                std::cout << "  ✓ Texture array created: " << maxWidth << "x" << maxHeight 
                          << " x " << totalTextures << " slices" << std::endl;
            }
            
            // **STEP 3: Batch upload data**
            const int MAX_TEXTURES_PER_BATCH = 64;
            
            // Collect UV scale factors for all textures
            std::vector<glm::vec2> uvScales;
            uvScales.reserve(totalTextures);
            
            if (totalTextures > MAX_TEXTURES_PER_BATCH) {
                std::cout << "  Using BATCH UPLOAD (" << MAX_TEXTURES_PER_BATCH 
                          << " textures per batch)" << std::endl;
                
                int numBatches = (totalTextures + MAX_TEXTURES_PER_BATCH - 1) / MAX_TEXTURES_PER_BATCH;
                
                for (int batchIdx = 0; batchIdx < numBatches; ++batchIdx) {
                    int batchStart = batchIdx * MAX_TEXTURES_PER_BATCH;
                    int batchEnd = std::min(batchStart + MAX_TEXTURES_PER_BATCH, totalTextures);
                    int batchSize = batchEnd - batchStart;
                    
                    std::cout << "  [Batch " << (batchIdx + 1) << "/" << numBatches << "] "
                              << "Uploading textures " << batchStart << "-" << (batchEnd - 1) 
                              << " (" << batchSize << " textures)" << std::endl;
                    
                    std::vector<std::shared_ptr<Texture>> batchTextures(
                        textures.begin() + batchStart,
                        textures.begin() + batchEnd
                    );
                    
                    try {
                        // Create independent command list for this batch
                        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> batchAllocator;
                        ThrowIfFailed(m_device->CreateCommandAllocator(
                            D3D12_COMMAND_LIST_TYPE_DIRECT,
                            IID_PPV_ARGS(&batchAllocator)),
                            "Failed to create batch allocator");
                        
                        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> tempBatchList;
                        ThrowIfFailed(m_device->CreateCommandList(
                            0,
                            D3D12_COMMAND_LIST_TYPE_DIRECT,
                            batchAllocator.Get(),
                            nullptr,
                            IID_PPV_ARGS(&tempBatchList)),
                            "Failed to create batch command list");
                        
                        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> batchCmdList;
                        ThrowIfFailed(tempBatchList.As(&batchCmdList),
                            "Failed to query batch command list interface");
                        
                        // Upload this batch (collect UV scales)
                        UploadTextureBatchData(batchCmdList.Get(), batchTextures, batchStart, maxWidth, maxHeight, &uvScales);
                        
                        // Execute and wait for this batch
                        ThrowIfFailed(batchCmdList->Close(), "Failed to close batch command list");
                        ID3D12CommandList* lists[] = { batchCmdList.Get() };
                        m_commandQueue->ExecuteCommandLists(1, lists);
                        WaitForGpu();
                        
                        std::cout << "    ✓ Batch " << (batchIdx + 1) << " completed" << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "    ✗ Batch " << (batchIdx + 1) << " failed: " << e.what() << std::endl;
                        throw;
                    }
                }
            } else {
                // Small texture set, upload all at once using caller's cmdList
                UploadTextureBatchData(cmdList, textures, 0, maxWidth, maxHeight, &uvScales);
            }
            
            // **STEP 4: Transition resource state (after all batches complete)**
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_textureAtlas.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );
            cmdList->ResourceBarrier(1, &barrier);
            
            // **STEP 5: Create SRV (once, after all uploads)**
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.MipLevels = 1;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize = totalTextures;
            
            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
            UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandle = { srvHandle.ptr + descriptorSize * 5 };
            
            m_device->CreateShaderResourceView(m_textureAtlas.Get(), &srvDesc, textureSrvHandle);
            
            std::cout << "  ✓ All textures uploaded and SRV created" << std::endl;
            
            // **STEP 6: Upload UV scale factors to GPU**
            // CRITICAL: Root signature requires this buffer, so create even if empty
            if (uvScales.empty()) {
                // Create dummy buffer with default scale (1.0, 1.0)
                uvScales.push_back(glm::vec2(1.0f, 1.0f));
                std::cout << "  No UV scales collected, using default (1.0, 1.0)" << std::endl;
            }
            
            size_t scalesBufferSize = sizeof(glm::vec2) * uvScales.size();
            std::cout << "  Creating texture scales buffer: " << uvScales.size() << " scales (" << scalesBufferSize << " bytes)" << std::endl;
            
            m_textureScalesBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, uvScales.data(), scalesBufferSize, m_textureScalesUpload);
            
            // Create SRV for scales buffer (slot 9 for t8)
            D3D12_SHADER_RESOURCE_VIEW_DESC scalesSrvDesc = {};
            scalesSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            scalesSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
            scalesSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            scalesSrvDesc.Buffer.FirstElement = 0;
            scalesSrvDesc.Buffer.NumElements = static_cast<UINT>(uvScales.size());
            scalesSrvDesc.Buffer.StructureByteStride = sizeof(glm::vec2);
            scalesSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            
            D3D12_CPU_DESCRIPTOR_HANDLE scalesSrvHandle = { srvHandle.ptr + descriptorSize * 9 };
            m_device->CreateShaderResourceView(m_textureScalesBuffer.Get(), &scalesSrvDesc, scalesSrvHandle);
            
            std::cout << "  ✓ Texture scales buffer created at slot 9 (t8)" << std::endl;
            
            } // end if (!useVirtualTextures)
        } // end if (!textures.empty())

        // Note: Old params3 removed - virtual texture flag now handled via MaterialData.flags
        
        // **收集材质层数据**
        m_scene->CollectAllMaterialLayers();
        const auto& materialLayers = m_scene->GetMaterialLayers();
        
        // **CREATE MATERIAL BUFFER**
        size_t materialBufferSize = sizeof(GPUMaterial) * materialsCPU.size();
        std::cout << "Creating material buffer: " << materialsCPU.size() << " materials, " 
                  << materialBufferSize << " bytes total, " << sizeof(GPUMaterial) << " bytes per material." << std::endl;
        if (materialBufferSize > 0) {
            m_materialBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, materialsCPU.data(), materialBufferSize, m_materialUpload);
            std::cout << "  Material buffer created: GPU address = " << m_materialBuffer->GetGPUVirtualAddress() << std::endl;
        } else {
            std::cout << "  ERROR: Material buffer size is 0!" << std::endl;
        }
        
        // **CREATE MATERIAL LAYERS BUFFER**
        // CRITICAL: Root signature requires this buffer, so create even if empty
        if (!materialLayers.empty()) {
            size_t layersBufferSize = sizeof(MaterialExtendedData) * materialLayers.size();
            std::cout << "Creating material layers buffer: " << materialLayers.size() << " layers, " 
                      << layersBufferSize << " bytes total, " << sizeof(MaterialExtendedData) << " bytes per layer." << std::endl;
            m_materialLayersBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, materialLayers.data(), layersBufferSize, m_materialLayersUpload);
            std::cout << "  Material layers buffer created: GPU address = " << m_materialLayersBuffer->GetGPUVirtualAddress() << std::endl;
        } else {
            // Create dummy buffer with one empty layer to satisfy root signature
            std::cout << "  No material layers found, creating dummy buffer" << std::endl;
            MaterialExtendedData dummyLayer = {};
            size_t layersBufferSize = sizeof(MaterialExtendedData);
            m_materialLayersBuffer = CreateDefaultBuffer(m_device.Get(), cmdList, &dummyLayer, layersBufferSize, m_materialLayersUpload);
            std::cout << "  Dummy material layers buffer created: GPU address = " << m_materialLayersBuffer->GetGPUVirtualAddress() << std::endl;
        }

        // Create output texture (UAV)
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = m_width;
        texDesc.Height = m_height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // Use float format for accumulation
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_outputTexture)));

        // Create UAV descriptor for output
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
        m_srvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Keep descriptor indices
        m_uavIndex_Output = 0;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // Match texture format
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = srvHandle;
        m_device->CreateUnorderedAccessView(m_outputTexture.Get(), nullptr, &uavDesc, uavHandle);

        // Create SRV for vertex buffer (structured buffer)
        m_srvIndex_Vertices = 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = static_cast<UINT>(vertices.size());
        srvDesc.Buffer.StructureByteStride = sizeof(GPUVertex);
        D3D12_CPU_DESCRIPTOR_HANDLE srvVertHandle = { srvHandle.ptr + m_srvUavDescriptorSize * m_srvIndex_Vertices };
        m_device->CreateShaderResourceView(m_vertexBuffer.Get(), &srvDesc, srvVertHandle);

        // Create SRV for index buffer (typed buffer - don't set StructureByteStride)
        m_srvIndex_Indices = 2;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvIdxDesc = {};
        srvIdxDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvIdxDesc.Format = DXGI_FORMAT_R32_UINT;
        srvIdxDesc.Buffer.FirstElement = 0;
        srvIdxDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvIdxDesc.Buffer.NumElements = static_cast<UINT>(indices.size());
        // For typed buffers, StructureByteStride must be 0
        D3D12_CPU_DESCRIPTOR_HANDLE srvIdxHandle = { srvHandle.ptr + m_srvUavDescriptorSize * m_srvIndex_Indices };
        m_device->CreateShaderResourceView(m_indexBuffer.Get(), &srvIdxDesc, srvIdxHandle);

        // Create SRV for triangle material indices (typed buffer)
        UINT srvIndex_TriangleMaterials = 3;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvTriMatDesc = {};
        srvTriMatDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvTriMatDesc.Format = DXGI_FORMAT_R32_UINT;
        srvTriMatDesc.Buffer.FirstElement = 0;
        srvTriMatDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvTriMatDesc.Buffer.NumElements = static_cast<UINT>(triangleMaterialIndices.size());
        D3D12_CPU_DESCRIPTOR_HANDLE srvTriMatHandle = { srvHandle.ptr + m_srvUavDescriptorSize * srvIndex_TriangleMaterials };
        m_device->CreateShaderResourceView(m_triangleMaterialBuffer.Get(), &srvTriMatDesc, srvTriMatHandle);

        // Create SRV for materials (structured buffer)
        m_srvIndex_Materials = 4;
        if (!m_materialBuffer) {
            std::cout << "  ERROR: Material buffer is null, cannot create SRV!" << std::endl;
        } else {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvMatDesc = {};
            srvMatDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvMatDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer
            srvMatDesc.Buffer.FirstElement = 0;
            srvMatDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvMatDesc.Buffer.NumElements = static_cast<UINT>(materialsCPU.size());  // Number of materials
            srvMatDesc.Buffer.StructureByteStride = sizeof(GPUMaterial);  // 96 bytes per material
            D3D12_CPU_DESCRIPTOR_HANDLE srvMatHandle = { srvHandle.ptr + m_srvUavDescriptorSize * m_srvIndex_Materials };
            m_device->CreateShaderResourceView(m_materialBuffer.Get(), &srvMatDesc, srvMatHandle);
            std::cout << "  Material SRV created as StructuredBuffer: " << materialsCPU.size() << " materials, stride=" << sizeof(GPUMaterial) << " bytes" << std::endl;
        }
        
        // Create SRV for material layers (structured buffer)
        // CRITICAL: Root signature requires this, buffer always exists now (dummy if empty)
        m_srvIndex_MaterialLayers = 5;  // Slot 5
        UINT numLayers = materialLayers.empty() ? 1 : static_cast<UINT>(materialLayers.size());  // At least 1 (dummy)
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvLayerDesc = {};
        srvLayerDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvLayerDesc.Format = DXGI_FORMAT_UNKNOWN;  // Structured buffer
        srvLayerDesc.Buffer.FirstElement = 0;
        srvLayerDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvLayerDesc.Buffer.NumElements = numLayers;
        srvLayerDesc.Buffer.StructureByteStride = sizeof(MaterialExtendedData);  // 32 bytes per layer
        D3D12_CPU_DESCRIPTOR_HANDLE srvLayerHandle = { srvHandle.ptr + m_srvUavDescriptorSize * m_srvIndex_MaterialLayers };
        m_device->CreateShaderResourceView(m_materialLayersBuffer.Get(), &srvLayerDesc, srvLayerHandle);
        std::cout << "  Material layers SRV created as StructuredBuffer: " << numLayers << " layers, stride=" << sizeof(MaterialExtendedData) << " bytes" << std::endl;

        // Transition output texture to UAV state
        cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_outputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        // Create default environment map if none exists (black 1x1 texture)
        if (!m_environmentMap) {
            std::cout << "Creating default environment map (black 1x1)..." << std::endl;
            
            D3D12_RESOURCE_DESC envTexDesc = {};
            envTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            envTexDesc.Width = 1;
            envTexDesc.Height = 1;
            envTexDesc.DepthOrArraySize = 1;
            envTexDesc.MipLevels = 1;
            envTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            envTexDesc.SampleDesc.Count = 1;
            envTexDesc.SampleDesc.Quality = 0;
            envTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            envTexDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            
            CD3DX12_HEAP_PROPERTIES envDefaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(m_device->CreateCommittedResource(
                &envDefaultHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &envTexDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_environmentMap)
            ));
            m_environmentMap->SetName(L"Default Environment Map");
            
            // Create upload buffer
            const UINT64 envUploadBufferSize = GetRequiredIntermediateSize(m_environmentMap.Get(), 0, 1);
            CD3DX12_HEAP_PROPERTIES envUploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
            auto envUploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(envUploadBufferSize);
            
            Microsoft::WRL::ComPtr<ID3D12Resource> envMapUpload;
            ThrowIfFailed(m_device->CreateCommittedResource(
                &envUploadHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &envUploadBufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&envMapUpload)
            ));
            
            // Black pixel data
            float blackPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            D3D12_SUBRESOURCE_DATA envSubresource = {};
            envSubresource.pData = blackPixel;
            envSubresource.RowPitch = 4 * sizeof(float);
            envSubresource.SlicePitch = envSubresource.RowPitch;
            
            // Upload to GPU
            UpdateSubresources(cmdList, m_environmentMap.Get(), envMapUpload.Get(), 0, 0, 1, &envSubresource);
            
            // Transition to shader resource
            auto envBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
                m_environmentMap.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );
            cmdList->ResourceBarrier(1, &envBarrier);
            
            std::cout << "  Default environment map created" << std::endl;
        }
        
        // Create SRV for environment map (slot 6)
        D3D12_SHADER_RESOURCE_VIEW_DESC envSrvDesc = {};
        envSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        envSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        envSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        envSrvDesc.Texture2D.MostDetailedMip = 0;
        envSrvDesc.Texture2D.MipLevels = 1;
        
        D3D12_CPU_DESCRIPTOR_HANDLE envMapSrvHandle = { srvHandle.ptr + m_srvUavDescriptorSize * 6 };
        m_device->CreateShaderResourceView(m_environmentMap.Get(), &envSrvDesc, envMapSrvHandle);
        std::cout << "  Environment map SRV created at slot 6" << std::endl;

        // Note: Do NOT close or execute here - caller will handle command list execution
        // Upload buffers are kept alive inside CreateDefaultBuffer until command list is executed
        
        // Check device status
        HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
        if (FAILED(deviceStatus)) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Device was removed after shader resource creation (HRESULT: 0x%08X)", deviceStatus);
            std::cerr << errorMsg << std::endl;
            throw std::runtime_error(errorMsg);
        }

        std::cout << "Shader resources uploaded: vertices=" << vertices.size() << " indices=" << indices.size() << " materials=" << materialsCPU.size() << std::endl;
    }

    // ==================== TEXTURE MANAGEMENT (CLEAN ARCHITECTURE) ====================
    
    /**
     * @brief Create texture array resource (Step 1: Resource Allocation)
     * This only allocates GPU memory, does not upload any data
     */
    void Renderer::CreateTextureArrayResource(int totalTextures, UINT maxWidth, UINT maxHeight) {
        if (m_textureAtlas != nullptr) {
            std::cout << "  Warning: Texture array already exists, skipping creation" << std::endl;
            return;
        }
        
        std::cout << "  [Resource Allocation] Creating texture array: " 
                  << maxWidth << "x" << maxHeight << " x " << totalTextures << " slices" << std::endl;
        
        D3D12_RESOURCE_DESC texArrayDesc = {};
        texArrayDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texArrayDesc.Width = maxWidth;
        texArrayDesc.Height = maxHeight;
        texArrayDesc.DepthOrArraySize = static_cast<UINT16>(totalTextures);
        texArrayDesc.MipLevels = 1;
        texArrayDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texArrayDesc.SampleDesc.Count = 1;
        texArrayDesc.SampleDesc.Quality = 0;
        texArrayDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texArrayDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &texArrayDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_textureAtlas)
        ), "Failed to create texture array");
        
        m_textureAtlas->SetName(L"Texture Array");
        std::cout << "  ✓ Texture array resource created" << std::endl;
    }
    
    /**
     * @brief Upload texture data to existing texture array (Step 2: Data Upload)
     * Assumes texture array resource was already created by CreateTextureArrayResource
     */
    void Renderer::UploadTextureBatchData(ID3D12GraphicsCommandList4* cmdList, 
                                         const std::vector<std::shared_ptr<Texture>>& textures, 
                                         int startIndex, UINT maxWidth, UINT maxHeight,
                                         std::vector<glm::vec2>* outUvScales) {
        if (textures.empty()) {
            return;
        }
        
        if (m_textureAtlas == nullptr) {
            throw std::runtime_error("Texture array must be created before uploading data");
        }
        
        std::cout << "  [Data Upload] Uploading " << textures.size() << " textures starting at index " << startIndex << std::endl;        // Calculate upload buffer size for this batch
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_textureAtlas.Get(), startIndex, textures.size());
        
        // Create temporary upload buffer for this batch
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        
        Microsoft::WRL::ComPtr<ID3D12Resource> batchUploadBuffer;
        ThrowIfFailed(m_device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&batchUploadBuffer)
        ), "Failed to create upload buffer");
        
        // Prepare subresource data
        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        std::vector<std::vector<BYTE>> textureData;
        textureData.reserve(textures.size());
        
        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& tex = textures[i];
            
            const unsigned char* srcData = tex->GetRawData();
            int srcChannels = tex->GetChannels();
            int srcWidth = tex->GetWidth();
            int srcHeight = tex->GetHeight();
            
            // Target dimensions must match array size (all slices must be same size)
            UINT dstWidth = maxWidth;
            UINT dstHeight = maxHeight;
            
            // Allocate texture buffer (full size, will be padded)
            std::vector<BYTE> rgba(maxWidth * maxHeight * 4, 0);
            
            // Resample texture using bilinear interpolation if size differs
            if (dstWidth != static_cast<UINT>(srcWidth) || dstHeight != static_cast<UINT>(srcHeight)) {
                std::cout << "    Resampling texture " << i << ": " << srcWidth << "x" << srcHeight 
                          << " -> " << dstWidth << "x" << dstHeight << std::endl;
                
                // Record UV scale factor (original / resampled)
                if (outUvScales) {
                    float scaleU = static_cast<float>(srcWidth) / static_cast<float>(dstWidth);
                    float scaleV = static_cast<float>(srcHeight) / static_cast<float>(dstHeight);
                    outUvScales->push_back(glm::vec2(scaleU, scaleV));
                }
                
                for (UINT y = 0; y < dstHeight; ++y) {
                    for (UINT x = 0; x < dstWidth; ++x) {
                        // Calculate source coordinates (bilinear interpolation)
                        float srcX = (static_cast<float>(x) + 0.5f) * srcWidth / dstWidth - 0.5f;
                        float srcY = (static_cast<float>(y) + 0.5f) * srcHeight / dstHeight - 0.5f;
                        
                        int x0 = static_cast<int>(std::floor(srcX));
                        int y0 = static_cast<int>(std::floor(srcY));
                        int x1 = std::min(x0 + 1, srcWidth - 1);
                        int y1 = std::min(y0 + 1, srcHeight - 1);
                        x0 = std::max(x0, 0);
                        y0 = std::max(y0, 0);
                        
                        float fx = srcX - x0;
                        float fy = srcY - y0;
                        
                        // Sample 4 neighboring pixels
                        for (int c = 0; c < 4; ++c) {
                            float val00, val10, val01, val11;
                            
                            if (c < srcChannels) {
                                int idx00 = (y0 * srcWidth + x0) * srcChannels + c;
                                int idx10 = (y0 * srcWidth + x1) * srcChannels + c;
                                int idx01 = (y1 * srcWidth + x0) * srcChannels + c;
                                int idx11 = (y1 * srcWidth + x1) * srcChannels + c;
                                
                                val00 = srcData[idx00];
                                val10 = srcData[idx10];
                                val01 = srcData[idx01];
                                val11 = srcData[idx11];
                            } else if (c == 3 && srcChannels < 4) {
                                // Alpha channel
                                val00 = val10 = val01 = val11 = 255.0f;
                            } else if (srcChannels == 1 && c < 3) {
                                // Grayscale to RGB
                                int idx00 = (y0 * srcWidth + x0) * srcChannels;
                                int idx10 = (y0 * srcWidth + x1) * srcChannels;
                                int idx01 = (y1 * srcWidth + x0) * srcChannels;
                                int idx11 = (y1 * srcWidth + x1) * srcChannels;
                                
                                val00 = srcData[idx00];
                                val10 = srcData[idx10];
                                val01 = srcData[idx01];
                                val11 = srcData[idx11];
                            } else {
                                val00 = val10 = val01 = val11 = 0.0f;
                            }
                            
                            // Bilinear interpolation
                            float val0 = val00 * (1.0f - fx) + val10 * fx;
                            float val1 = val01 * (1.0f - fx) + val11 * fx;
                            float val = val0 * (1.0f - fy) + val1 * fy;
                            
                            int dstIdx = (y * maxWidth + x) * 4 + c;
                            rgba[dstIdx] = static_cast<BYTE>(std::min(std::max(val, 0.0f), 255.0f));
                        }
                    }
                }
            } else {
                // Direct copy (no resampling needed)
                if (outUvScales) {
                    outUvScales->push_back(glm::vec2(1.0f, 1.0f));  // No scaling
                }
                
                for (int y = 0; y < srcHeight; ++y) {
                    for (int x = 0; x < srcWidth; ++x) {
                        int srcIdx = (y * srcWidth + x) * srcChannels;
                        int dstIdx = (y * maxWidth + x) * 4;
                        
                        if (srcChannels >= 3) {
                            rgba[dstIdx + 0] = srcData[srcIdx + 0];
                            rgba[dstIdx + 1] = srcData[srcIdx + 1];
                            rgba[dstIdx + 2] = srcData[srcIdx + 2];
                            rgba[dstIdx + 3] = (srcChannels == 4) ? srcData[srcIdx + 3] : 255;
                        } else if (srcChannels == 1) {
                            rgba[dstIdx + 0] = srcData[srcIdx];
                            rgba[dstIdx + 1] = srcData[srcIdx];
                            rgba[dstIdx + 2] = srcData[srcIdx];
                            rgba[dstIdx + 3] = 255;
                        }
                    }
                }
            }
            
            textureData.push_back(std::move(rgba));
            
            D3D12_SUBRESOURCE_DATA subresource = {};
            subresource.pData = textureData.back().data();
            subresource.RowPitch = maxWidth * 4;
            subresource.SlicePitch = subresource.RowPitch * maxHeight;
            subresources.push_back(subresource);
        }
        
        // Upload to GPU
        UpdateSubresources(cmdList, m_textureAtlas.Get(), batchUploadBuffer.Get(),
                          0, startIndex, static_cast<UINT>(subresources.size()), subresources.data());
        
        // Keep upload buffer alive (stored as member to prevent premature destruction)
        m_textureUpload = batchUploadBuffer;
        
        std::cout << "  ✓ Batch data uploaded to GPU" << std::endl;
    }
    
    /**
     * @brief Finalize texture array (Step 3: State Transition & Descriptor Creation)
     * Transitions resource to shader-readable state and creates SRV
     */
    void Renderer::FinalizeTextureArray(ID3D12GraphicsCommandList4* cmdList, int totalTextures) {
        if (m_textureAtlas == nullptr) {
            throw std::runtime_error("Texture array must exist before finalization");
        }
        
        std::cout << "  [Finalization] Transitioning texture array to shader resource state" << std::endl;
        
        // Transition resource state
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_textureAtlas.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);
        
        // Create SRV in descriptor heap (slot 5 for t3)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = totalTextures;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandle = { srvHandle.ptr + descriptorSize * 5 };
        
        m_device->CreateShaderResourceView(m_textureAtlas.Get(), &srvDesc, textureSrvHandle);
        
        std::cout << "  ✓ Texture array finalized: " << totalTextures << " textures ready for rendering" << std::endl;
    }
    
    /**
     * @brief Create Virtual Texture SRVs (for Virtual Texture System)
     * Creates SRVs for physical page cache (t5) and indirection texture (t6)
     */
    void Renderer::CreateVirtualTextureSRVs() {
        if (!m_useVirtualTextures) {
            return;
        }
        
        std::cout << "  Creating Virtual Texture SRVs..." << std::endl;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        
        // Slot 7: Virtual Texture Cache (t5) - physical page cache
        D3D12_CPU_DESCRIPTOR_HANDLE virtualTextureCacheSrv = { srvHandle.ptr + descriptorSize * 7 };
        
        // Slot 8: Indirection Texture (t6) - lookup table
        D3D12_CPU_DESCRIPTOR_HANDLE indirectionTextureSrv = { srvHandle.ptr + descriptorSize * 8 };
        
        // Call Virtual Texture System to create its SRVs
        m_virtualTextureSystem.CreateShaderResourceView(m_device.Get(), virtualTextureCacheSrv, indirectionTextureSrv);
        
        std::cout << "  ✓ Virtual Texture SRVs created (slots 7 and 8)" << std::endl;
    }

    // ==================== ENVIRONMENT MAP ====================

    Microsoft::WRL::ComPtr<ID3D12Resource> Renderer::UploadEnvironmentMap(ID3D12GraphicsCommandList4* cmdList, const std::shared_ptr<Texture>& envMap) {
        if (!envMap || !envMap->IsHDR()) {
            std::cout << "  No valid HDR environment map to upload" << std::endl;
            return nullptr;
        }

        std::cout << "  Uploading HDR environment map: " << envMap->GetWidth() << "x" << envMap->GetHeight() << std::endl;
        
        // Create texture resource for environment map
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = envMap->GetWidth();
        texDesc.Height = envMap->GetHeight();
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;  // HDR format
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        
        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(m_device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_environmentMap)
        ));
        m_environmentMap->SetName(L"Environment Map");
        
        // Create upload buffer
        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_environmentMap.Get(), 0, 1);
        
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        
        Microsoft::WRL::ComPtr<ID3D12Resource> envMapUpload;
        ThrowIfFailed(m_device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&envMapUpload)
        ));
        
        // Prepare subresource data (convert to RGBA if needed)
        const float* hdrData = envMap->GetHDRData();
        int channels = envMap->GetChannels();
        int width = envMap->GetWidth();
        int height = envMap->GetHeight();
        
        std::vector<float> rgba(width * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int srcIdx = (y * width + x) * channels;
                int dstIdx = (y * width + x) * 4;
                
                if (channels >= 3) {
                    rgba[dstIdx + 0] = hdrData[srcIdx + 0];
                    rgba[dstIdx + 1] = hdrData[srcIdx + 1];
                    rgba[dstIdx + 2] = hdrData[srcIdx + 2];
                    rgba[dstIdx + 3] = (channels == 4) ? hdrData[srcIdx + 3] : 1.0f;
                } else if (channels == 1) {
                    rgba[dstIdx + 0] = hdrData[srcIdx];
                    rgba[dstIdx + 1] = hdrData[srcIdx];
                    rgba[dstIdx + 2] = hdrData[srcIdx];
                    rgba[dstIdx + 3] = 1.0f;
                }
            }
        }
        
        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = rgba.data();
        subresource.RowPitch = width * 4 * sizeof(float);
        subresource.SlicePitch = subresource.RowPitch * height;
        
        // Upload to GPU
        UpdateSubresources(cmdList, m_environmentMap.Get(), envMapUpload.Get(), 0, 0, 1, &subresource);
        
        // Transition to shader resource
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_environmentMap.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);
        
        // Create SRV in descriptor heap (slot 6 for environment map)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
        UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE envMapSrvHandle = { srvHandle.ptr + descriptorSize * 6 };  // Slot 6 for environment map
        
        m_device->CreateShaderResourceView(m_environmentMap.Get(), &srvDesc, envMapSrvHandle);
        
        std::cout << "  ✓ Environment map uploaded: " << width << "x" << height << std::endl;
        
        // Return upload buffer to keep it alive until GPU finishes using it
        return envMapUpload;
    }

    void Renderer::CreateShaderBindingTable() {
        if (!m_dxrSupported || !m_dxrStateObject) {
            std::cout << "Skipping SBT creation: DXR not supported or state object missing" << std::endl;
            return;
        }

        // Get shader identifiers from state object
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> stateObjectProps;
        ThrowIfFailed(m_dxrStateObject.As(&stateObjectProps));

        void* rayGenID = stateObjectProps->GetShaderIdentifier(L"RayGen");
        void* missID = stateObjectProps->GetShaderIdentifier(L"Miss");
        void* hitGroupID = stateObjectProps->GetShaderIdentifier(L"HitGroup");

        if (!rayGenID || !missID || !hitGroupID) {
            throw std::runtime_error("Failed to get shader identifiers");
        }

        // Shader record size: identifier (D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES = 32) + optional root arguments
        const UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32 bytes
        const UINT shaderRecordSize = shaderIdentifierSize; // No root arguments for now
        
        // Align to D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT (64 bytes)
        const UINT shaderRecordAlignedSize = (shaderRecordSize + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT - 1) 
                                            & ~(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT - 1);

        // Store entry size for later use
        m_sbtEntrySize = shaderRecordAlignedSize;

        // Calculate offsets
        m_sbtRayGenOffset = 0;
        m_sbtMissOffset = m_sbtRayGenOffset + shaderRecordAlignedSize;
        m_sbtHitGroupOffset = m_sbtMissOffset + shaderRecordAlignedSize;
        
        UINT sbtSize = m_sbtHitGroupOffset + shaderRecordAlignedSize;

        // Create SBT upload buffer
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(sbtSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_sbtBuffer)));

        // Map and write shader records
        uint8_t* mappedData;
        ThrowIfFailed(m_sbtBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));
        
        // Write RayGen record
        memcpy(mappedData + m_sbtRayGenOffset, rayGenID, shaderIdentifierSize);
        
        // Write Miss record
        memcpy(mappedData + m_sbtMissOffset, missID, shaderIdentifierSize);
        
        // Write HitGroup record
        memcpy(mappedData + m_sbtHitGroupOffset, hitGroupID, shaderIdentifierSize);
        
        m_sbtBuffer->Unmap(0, nullptr);

        std::cout << "Shader Binding Table created: RayGen@" << m_sbtRayGenOffset 
                  << " Miss@" << m_sbtMissOffset 
                  << " HitGroup@" << m_sbtHitGroupOffset 
                  << " EntrySize=" << m_sbtEntrySize << std::endl;
    }

    void Renderer::OnUpdate() {
        // Update camera, animations, etc.
    }

    void Renderer::OnRender() {
        // If offline rendering is in progress, skip all GPU operations
        // Just return immediately to keep main thread responsive
        if (m_isRendering.load()) {
            // Sleep briefly to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            return;
        }
        
        // Normal rendering when not doing offline render
        try {
            PopulateCommandList();
            ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
            m_swapChain->Present(1, 0);
            MoveToNextFrame();
        } catch (...) {
            // Completely suppress all errors
        }
    }

    void Renderer::PopulateCommandList() {
        try {
            // Wait for GPU to finish with this frame's command allocator
            const UINT64 completedValue = m_fence->GetCompletedValue();
            const UINT64 expectedValue = m_fenceValue - 1;
            
            if (completedValue < expectedValue) {
                m_fence->SetEventOnCompletion(expectedValue, m_fenceEvent);
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
            
            // Reset command allocator and list
            m_commandAllocators[m_frameIndex]->Reset();
            m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

            // NO RAYTRACING - Only clear background and render ImGui
            // All raytracing is done offline in RenderToFile()
            
            // Transition render target to render target state
            D3D12_RESOURCE_BARRIER rtBarrier = {};
            rtBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            rtBarrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
            rtBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            rtBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            rtBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &rtBarrier);
            
            // Clear render target to a dark gray background
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
            const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            
            // Set render target for ImGui
            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            
            // Render ImGui
            {
            // Transition to render target
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &barrier);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
            const float clearColor[] = { 0.1f, 0.2f, 0.4f, 1.0f };
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            
            // Render ImGui on top
            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            ID3D12DescriptorHeap* imguiHeaps[] = { m_imguiSrvHeap.Get() };
            m_commandList->SetDescriptorHeaps(1, imguiHeaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            m_commandList->ResourceBarrier(1, &barrier);
        }

        ThrowIfFailed(m_commandList->Close(), "Failed to close command list");
        
        } catch (const std::exception& e) {
            std::cerr << "[PopulateCommandList] Exception: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "[PopulateCommandList] Unknown exception" << std::endl;
            throw std::runtime_error("Unknown exception in PopulateCommandList");
        }
    }



    void Renderer::OnDestroy() {
        WaitForGpu();
        if (m_fenceEvent) {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        std::cout << "Renderer destroyed" << std::endl;
    }

    void Renderer::OnResize(UINT width, UINT height) {
        if (width == 0 || height == 0 || width == m_width && height == m_height) {
            return;
        }

        m_width = width;
        m_height = height;

        WaitForGpu();

        // Release old render targets
        for (UINT i = 0; i < FrameCount; i++) {
            m_renderTargets[i].Reset();
        }

        // Resize swap chain buffers
        ThrowIfFailed(m_swapChain->ResizeBuffers(
            FrameCount,
            m_width,
            m_height,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0
        ));

        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Recreate render targets
        CreateRenderTargets();
    }

    void Renderer::WaitForGpu() {
        if (m_fence && m_commandQueue) {
            // Use a separate fence value to avoid interfering with frame synchronization
            const UINT64 fenceValue = m_fenceValue;
            ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue), "Failed to signal fence");
            m_fenceValue++;

            // Wait for the fence to be signaled
            if (m_fence->GetCompletedValue() < fenceValue) {
                ThrowIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent), "Failed to set fence event");
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
        }
    }

    void Renderer::MoveToNextFrame() {
        const UINT64 currentFenceValue = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue), "Failed to signal fence in MoveToNextFrame");
        m_fenceValue++;

        // Update frame index
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Wait for the previous frame's commands to complete
        if (m_fence->GetCompletedValue() < currentFenceValue) {
            ThrowIfFailed(m_fence->SetEventOnCompletion(currentFenceValue, m_fenceEvent), "Failed to set fence event in MoveToNextFrame");
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    void Renderer::SetEnvironmentMap(const std::string& path) {
        std::cout << "Loading environment map from: " << path << std::endl;
        
        // Load HDR/EXR texture
        auto envMap = std::make_shared<Texture>();
        envMap->SetType(TextureType::Environment);
        if (!envMap->LoadFromFile(path)) {
            std::cerr << "Failed to load environment map: " << path << std::endl;
            throw std::runtime_error("Failed to load environment map file");
        }
        
        if (!envMap->IsHDR()) {
            std::cerr << "Environment map is not HDR format: " << path << std::endl;
            throw std::runtime_error("Environment map is not HDR format");
        }
        
        // Create independent command allocator for environment map loading
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> envMapAllocator;
        ThrowIfFailed(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&envMapAllocator)),
            "Failed to create environment map command allocator");
        
        // Create independent command list
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> envMapCmdList;
        ThrowIfFailed(m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            envMapAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&envMapCmdList)),
            "Failed to create environment map command list");
        
        // Upload to GPU using independent command list
        // Keep upload buffer alive until GPU finishes
        auto envMapUploadBuffer = UploadEnvironmentMap(envMapCmdList.Get(), envMap);
        
        // Execute and wait
        ThrowIfFailed(envMapCmdList->Close(), "Failed to close environment map command list");
        ID3D12CommandList* cmdLists[] = { envMapCmdList.Get() };
        m_commandQueue->ExecuteCommandLists(1, cmdLists);
        WaitForGpu();
        
        // Now it's safe to let envMapUploadBuffer be destroyed
        
        std::cout << "Environment map loaded successfully" << std::endl;
    }

    void Renderer::ClearEnvironmentMap() {
        std::cout << "Clearing environment map" << std::endl;
        
        // Wait for GPU to finish using the resource
        WaitForGpu();
        
        // Release the environment map resource
        m_environmentMap.Reset();
        
        std::cout << "Environment map cleared" << std::endl;
    }

} // namespace ACG
