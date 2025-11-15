#include "Renderer.h"
#include "BVH.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ACG {

Renderer::Renderer() 
    : m_device(nullptr)
    , m_context(nullptr)
    , m_swapChain(nullptr)
    , m_pathTracingShader(nullptr)
    , m_accumShader(nullptr)
    , m_renderTarget(nullptr)
    , m_renderTargetUAV(nullptr)
    , m_triangleBuffer(nullptr)
    , m_triangleSRV(nullptr)
    , m_materialBuffer(nullptr)
    , m_materialSRV(nullptr)
    , m_lightBuffer(nullptr)
    , m_lightSRV(nullptr)
    , m_bvhBuffer(nullptr)
    , m_bvhSRV(nullptr)
    , m_bvhTriangleIndicesBuffer(nullptr)
    , m_bvhTriangleIndicesSRV(nullptr)
    , m_constantBuffer(nullptr)
    , m_numTriangles(0)
    , m_numMaterials(0)
    , m_numLights(0)
    , m_numBVHNodes(0)
    , m_numBVHTriangleIndices(0)
    , m_width(0)
    , m_height(0)
    , m_samplesPerPixel(1)
    , m_maxBounces(5)
    , m_rrDepth(3)
    , m_environmentLight(0.5f)
    , m_frameCount(0)
    , m_accumulatedSamples(0)
    , m_currentAccumIndex(0)
{
    m_accumulationTexture[0] = nullptr;
    m_accumulationTexture[1] = nullptr;
    m_accumulationUAV[0] = nullptr;
    m_accumulationUAV[1] = nullptr;
    m_accumulationSRV[0] = nullptr;
    m_accumulationSRV[1] = nullptr;
}

Renderer::~Renderer() {
    Cleanup();
}

bool Renderer::Initialize(int width, int height) {
    m_width = width;
    m_height = height;
    
    // TODO: Initialize DirectX 11
    if (!InitializeD3D()) {
        return false;
    }
    
    // TODO: Create compute shaders
    if (!CreateShaders()) {
        return false;
    }
    
    // TODO: Create render buffers
    if (!CreateBuffers()) {
        return false;
    }
    
    std::cout << "Renderer initialized: " << width << "x" << height << std::endl;
    return true;
}

void Renderer::ResetAccumulation() {
    m_accumulatedSamples = 0;
    m_frameCount = 0;
    m_currentAccumIndex = 0;
    
    // Clear both accumulation textures to zero
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (m_context) {
        if (m_accumulationUAV[0]) m_context->ClearUnorderedAccessViewFloat(m_accumulationUAV[0], clearColor);
        if (m_accumulationUAV[1]) m_context->ClearUnorderedAccessViewFloat(m_accumulationUAV[1], clearColor);
    }
}

void Renderer::RenderFrame(const Scene& scene, const Camera& camera, int samplesThisFrame) {
    if (!m_device || !m_context || !m_pathTracingShader) {
        return;
    }
    
    // Upload scene data on first frame
    if (m_frameCount == 0) {
        if (!UploadSceneData(scene)) {
            std::cerr << "Failed to upload scene data" << std::endl;
            return;
        }
    }
    
    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        struct GPUCamera {
            float position[3]; float _pad0;
            float direction[3]; float _pad1;
            float right[3]; float _pad2;
            float up[3]; float _pad3;
            float fov; float aspectRatio; float aperture; float focusDistance;
        };
        struct RenderParams {
            GPUCamera camera;
            uint32_t frameIndex;
            uint32_t samplesPerPixel;
            uint32_t maxBounces;
            uint32_t numTriangles;
            uint32_t numMaterials;
            uint32_t accumulatedSamples;
            float environmentLight;
            uint32_t _pad2;
            uint32_t resX;
            uint32_t resY;
            uint32_t _pad3[2];
        };
        
        RenderParams* params = static_cast<RenderParams*>(mappedResource.pData);
        glm::vec3 pos = camera.GetPosition();
        glm::vec3 dir = camera.GetDirection();
        glm::vec3 right = camera.GetRight();
        glm::vec3 up = camera.GetUp();
        
        params->camera.position[0] = pos.x;
        params->camera.position[1] = pos.y;
        params->camera.position[2] = pos.z;
        params->camera._pad0 = 0.0f;
        
        params->camera.direction[0] = dir.x;
        params->camera.direction[1] = dir.y;
        params->camera.direction[2] = dir.z;
        params->camera._pad1 = 0.0f;
        
        params->camera.right[0] = right.x;
        params->camera.right[1] = right.y;
        params->camera.right[2] = right.z;
        params->camera._pad2 = 0.0f;
        
        params->camera.up[0] = up.x;
        params->camera.up[1] = up.y;
        params->camera.up[2] = up.z;
        params->camera._pad3 = 0.0f;
        
        params->camera.fov = camera.GetFOV();
        params->camera.aspectRatio = static_cast<float>(m_width) / m_height;
        params->camera.aperture = camera.GetAperture();
        params->camera.focusDistance = camera.GetFocusDistance();
        
        params->frameIndex = m_frameCount;
        params->samplesPerPixel = samplesThisFrame;  // Use samplesThisFrame instead of total
        params->maxBounces = m_maxBounces;
        params->numTriangles = m_numTriangles;
        params->numMaterials = m_numMaterials;
        params->accumulatedSamples = m_accumulatedSamples;  // Pass current accumulated count
        params->environmentLight = m_environmentLight;
        params->_pad2 = 0;
        params->resX = m_width;
        params->resY = m_height;
        params->_pad3[0] = 0;
        params->_pad3[1] = 0;
        
        m_context->Unmap(m_constantBuffer, 0);
    }
    
    // Bind resources
    m_context->CSSetShader(m_pathTracingShader, nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, &m_constantBuffer);
    
    // Ping-pong: read from current, write to next
    int readIndex = m_currentAccumIndex;
    int writeIndex = 1 - m_currentAccumIndex;
    
    ID3D11ShaderResourceView* srvs[5] = { 
        m_triangleSRV, 
        m_materialSRV, 
        m_accumulationSRV[readIndex],  // Read from current accumulation
        m_bvhSRV, 
        m_bvhTriangleIndicesSRV 
    };
    m_context->CSSetShaderResources(0, 5, srvs);
    
    // Bind both render target and accumulation texture as UAVs
    ID3D11UnorderedAccessView* uavs[2] = { 
        m_renderTargetUAV, 
        m_accumulationUAV[writeIndex]  // Write to next accumulation
    };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    
    // Dispatch
    UINT groupsX = (m_width + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);
    m_context->Flush();
    
    // Check device status
    HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus)) {
        std::cerr << "ERROR: Device removed! HRESULT: 0x" << std::hex << deviceStatus << std::dec << std::endl;
    }
    
    // Unbind resources
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr };
    m_context->CSSetShaderResources(0, 4, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    
    // Swap accumulation buffers for next frame
    m_currentAccumIndex = writeIndex;
    
    m_frameCount++;
    m_accumulatedSamples += samplesThisFrame;
}

// Legacy single-shot render (calls RenderFrame once)
void Renderer::Render(const Scene& scene, const Camera& camera) {
    if (!m_device || !m_context || !m_pathTracingShader) {
        return;
    }
    
    // Upload scene data on first frame
    if (m_frameCount == 0) {
        if (!UploadSceneData(scene)) {
            std::cerr << "Failed to upload scene data" << std::endl;
            return;
        }
    }
    
    // Update constant buffer - Must match HLSL structure exactly
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        struct GPUCamera {
            float position[3]; float _pad0;
            float direction[3]; float _pad1;
            float right[3]; float _pad2;
            float up[3]; float _pad3;
            float fov; float aspectRatio; float aperture; float focusDistance;
        };
        struct RenderParams {
            GPUCamera camera;           // 80 bytes
            uint32_t frameIndex;        // 4
            uint32_t samplesPerPixel;   // 4
            uint32_t maxBounces;        // 4
            uint32_t numTriangles;      // 4
            uint32_t numMaterials;      // 4
            uint32_t _pad0;             // 4
            uint32_t _pad1;             // 4
            uint32_t _pad2;             // 4
            uint32_t resX;              // 4
            uint32_t resY;              // 4
            uint32_t _pad3[2];          // 8
        };  // Total: 128 bytes
        
        RenderParams* params = static_cast<RenderParams*>(mappedResource.pData);
        glm::vec3 pos = camera.GetPosition();
        glm::vec3 dir = camera.GetDirection();
        glm::vec3 right = camera.GetRight();
        glm::vec3 up = camera.GetUp();
        
        params->camera.position[0] = pos.x;
        params->camera.position[1] = pos.y;
        params->camera.position[2] = pos.z;
        params->camera._pad0 = 0.0f;
        
        params->camera.direction[0] = dir.x;
        params->camera.direction[1] = dir.y;
        params->camera.direction[2] = dir.z;
        params->camera._pad1 = 0.0f;
        
        params->camera.right[0] = right.x;
        params->camera.right[1] = right.y;
        params->camera.right[2] = right.z;
        params->camera._pad2 = 0.0f;
        
        params->camera.up[0] = up.x;
        params->camera.up[1] = up.y;
        params->camera.up[2] = up.z;
        params->camera._pad3 = 0.0f;
        
        params->camera.fov = camera.GetFOV();
        params->camera.aspectRatio = static_cast<float>(m_width) / m_height;
        params->camera.aperture = camera.GetAperture();
        params->camera.focusDistance = camera.GetFocusDistance();
        
        params->frameIndex = m_frameCount;
        params->samplesPerPixel = m_samplesPerPixel;
        params->maxBounces = m_maxBounces;
        params->numTriangles = m_numTriangles;
        params->numMaterials = m_numMaterials;
        params->_pad0 = 0;
        params->_pad1 = 0;
        params->_pad2 = 0;
        params->resX = m_width;
        params->resY = m_height;
        params->_pad3[0] = 0;
        params->_pad3[1] = 0;
        
        m_context->Unmap(m_constantBuffer, 0);
    }
    
    // Bind resources
    m_context->CSSetShader(m_pathTracingShader, nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, &m_constantBuffer);
    
    // Bind shader resources (only bind valid ones)
    ID3D11ShaderResourceView* srvs[3] = { m_triangleSRV, m_materialSRV, nullptr };
    m_context->CSSetShaderResources(0, 2, srvs);  // Only bind 2 resources
    
    m_context->CSSetUnorderedAccessViews(0, 1, &m_renderTargetUAV, nullptr);
    
    // Dispatch compute shader
    UINT groupsX = (m_width + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    
    glm::vec3 pos = camera.GetPosition();
    glm::vec3 dir = camera.GetDirection();
    glm::vec3 right = camera.GetRight();
    glm::vec3 up = camera.GetUp();
    
    std::cout << "Camera vectors - Pos:[" << pos.x << "," << pos.y << "," << pos.z << "]" << std::endl;
    std::cout << "                Dir:[" << dir.x << "," << dir.y << "," << dir.z << "]" << std::endl;
    std::cout << "                Right:[" << right.x << "," << right.y << "," << right.z << "]" << std::endl;
    std::cout << "                Up:[" << up.x << "," << up.y << "," << up.z << "]" << std::endl;
    
    m_context->Dispatch(groupsX, groupsY, 1);
    
    // Flush to ensure GPU work is submitted
    m_context->Flush();
    
    // Check device status
    HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus)) {
        std::cerr << "ERROR: Device removed! HRESULT: 0x" << std::hex << deviceStatus << std::dec << std::endl;
    } else {
        std::cout << "Device status: OK" << std::endl;
    }
    
    // Unbind resources
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    m_context->CSSetShaderResources(0, 3, nullSRVs);
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    
    m_frameCount++;
    
    if (m_frameCount % 10 == 0) {
        std::cout << "Frame " << m_frameCount << " rendered" << std::endl;
    }
}

void Renderer::GetRenderResult(unsigned char* pixels) {
    if (!m_device || !m_context || !m_renderTarget || !pixels) {
        return;
    }
    
    // Create staging texture for readback
    D3D11_TEXTURE2D_DESC texDesc;
    m_renderTarget->GetDesc(&texDesc);
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        return;
    }
    
    // Copy from GPU to staging texture
    std::cout << "Copying GPU texture to staging..." << std::endl;
    m_context->CopyResource(stagingTexture, m_renderTarget);
    
    // Ensure copy is complete
    m_context->Flush();
    std::cout << "Copy complete, mapping for readback..." << std::endl;
    
    // Map and read data
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    std::cout << "Map result: HRESULT=0x" << std::hex << hr << std::dec << std::endl;
    
    if (FAILED(hr)) {
        // Check if device was removed
        HRESULT deviceRemoved = m_device->GetDeviceRemovedReason();
        std::cerr << "ERROR: Failed to map staging texture!" << std::endl;
        std::cerr << "Device removed reason: 0x" << std::hex << deviceRemoved << std::dec << std::endl;
        
        if (deviceRemoved == DXGI_ERROR_DEVICE_HUNG) {
            std::cerr << "Device hung - GPU took too long to respond (TDR timeout)" << std::endl;
            std::cerr << "Large scenes may require splitting render into smaller dispatches or using async compute" << std::endl;
        } else if (deviceRemoved == DXGI_ERROR_DEVICE_REMOVED) {
            std::cerr << "Device removed - GPU driver crashed or was reset" << std::endl;
        } else if (deviceRemoved == DXGI_ERROR_DEVICE_RESET) {
            std::cerr << "Device reset - GPU was reset by the OS" << std::endl;
        } else if (deviceRemoved == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
            std::cerr << "Driver internal error" << std::endl;
        }
        
        stagingTexture->Release();
        return;
    }
    
    if (SUCCEEDED(hr)) {
        std::cout << "Mapped successfully, RowPitch=" << mapped.RowPitch << std::endl;
        float* src = static_cast<float*>(mapped.pData);
        
        // Validate mapped data
        if (src == nullptr) {
            std::cerr << "ERROR: Mapped pointer is null!" << std::endl;
            m_context->Unmap(stagingTexture, 0);
            stagingTexture->Release();
            return;
        }
        
        // Calculate row pitch in floats
        size_t rowPitchFloats = mapped.RowPitch / sizeof(float);
        
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                size_t srcIdx = static_cast<size_t>(y) * rowPitchFloats + static_cast<size_t>(x) * 4;
                size_t dstIdx = static_cast<size_t>(y * m_width + x) * 4;
                
                // Bounds check
                if (dstIdx + 3 >= static_cast<size_t>(m_width * m_height * 4)) {
                    std::cerr << "ERROR: Destination index out of bounds at (" << x << "," << y << ")" << std::endl;
                    break;
                }
                
                float r = src[srcIdx + 0];
                float g = src[srcIdx + 1];
                float b = src[srcIdx + 2];
                
                // Convert float to byte with gamma correction
                pixels[dstIdx + 0] = static_cast<unsigned char>(std::pow(std::clamp(r, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 1] = static_cast<unsigned char>(std::pow(std::clamp(g, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 2] = static_cast<unsigned char>(std::pow(std::clamp(b, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 3] = 255;
            }
        }
        
        m_context->Unmap(stagingTexture, 0);
    }
    
    stagingTexture->Release();
}

void Renderer::SetSamplesPerPixel(int samples) {
    m_samplesPerPixel = samples;
}

void Renderer::SetMaxBounces(int bounces) {
    m_maxBounces = bounces;
}

void Renderer::SetRussianRouletteDepth(int depth) {
    m_rrDepth = depth;
}

void Renderer::SetEnvironmentLight(float intensity) {
    m_environmentLight = intensity;
}

bool Renderer::InitializeD3D() {
    HRESULT hr;
    
    // Create device and context
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);
    
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,                    // Adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // Software rasterizer module
        createDeviceFlags,
        featureLevels,
        numFeatureLevels,
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );
    
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device" << std::endl;
        return false;
    }
    
    std::cout << "D3D11 device created successfully" << std::endl;
    return true;
}

bool Renderer::CreateShaders() {
    HRESULT hr;
    
    // Get executable directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    // Construct shader path (shaders are copied to bin/shaders/ during build)
    std::wstring shaderPath = exeDir + L"\\shaders\\PathTracing.hlsl";
    
    std::wcout << L"Loading shader from: " << shaderPath << std::endl;
    
    // Compile path tracing compute shader
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    shaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    
    hr = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "cs_5_0",
        shaderFlags,
        0,
        &shaderBlob,
        &errorBlob
    );
    
    if (FAILED(hr)) {
        if (errorBlob) {
            std::string error(static_cast<char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            std::ofstream errorFile("shader_error.txt");
            errorFile << error;
            errorFile.close();
            std::cerr << "Shader compilation error (see shader_error.txt): " << std::endl;
            std::cerr << error << std::endl;
            errorBlob->Release();
        } else {
            std::cerr << "Shader compilation failed with HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
        }
        return false;
    }
    
    // Check for warnings even if compilation succeeded
    if (errorBlob) {
        std::cout << "Shader compilation warnings: " 
                 << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
        errorBlob->Release();
    }
    
    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &m_pathTracingShader
    );
    
    shaderBlob->Release();
    
    if (FAILED(hr)) {
        std::cerr << "Failed to create compute shader" << std::endl;
        return false;
    }
    
    std::cout << "Shaders compiled successfully" << std::endl;
    return true;
}

bool Renderer::CreateBuffers() {
    HRESULT hr;
    
    // Create render target texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    
    hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_renderTarget);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target texture" << std::endl;
        return false;
    }
    
    // Create UAV for compute shader output
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = texDesc.Format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    
    hr = m_device->CreateUnorderedAccessView(m_renderTarget, &uavDesc, &m_renderTargetUAV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create UAV" << std::endl;
        return false;
    }
    
    // Create ping-pong accumulation textures (for progressive rendering)
    for (int i = 0; i < 2; ++i) {
        hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_accumulationTexture[i]);
        if (FAILED(hr)) {
            std::cerr << "Failed to create accumulation texture " << i << std::endl;
            return false;
        }
        
        // Create UAV for accumulation texture
        hr = m_device->CreateUnorderedAccessView(m_accumulationTexture[i], &uavDesc, &m_accumulationUAV[i]);
        if (FAILED(hr)) {
            std::cerr << "Failed to create accumulation UAV " << i << std::endl;
            return false;
        }
        
        // Create SRV for accumulation texture (to read previous frame)
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        
        hr = m_device->CreateShaderResourceView(m_accumulationTexture[i], &srvDesc, &m_accumulationSRV[i]);
        if (FAILED(hr)) {
            std::cerr << "Failed to create accumulation SRV " << i << std::endl;
            return false;
        }
        
        // Initialize accumulation texture to zero
        float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        m_context->ClearUnorderedAccessViewFloat(m_accumulationUAV[i], clearColor);
    }
    
    m_currentAccumIndex = 0;
    
    // Create constant buffer
    // RenderParams structure: Camera (80) + 6 uints (24) + padding (8) + 2 uints (8) = 120 bytes
    // Must be aligned to 16 bytes for D3D11
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 128;  // Next multiple of 16 after 120
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create constant buffer" << std::endl;
        return false;
    }
    
    std::cout << "Render buffers created" << std::endl;
    return true;
}

void Renderer::CleanupSceneBuffers() {
    if (m_triangleSRV) { m_triangleSRV->Release(); m_triangleSRV = nullptr; }
    if (m_triangleBuffer) { m_triangleBuffer->Release(); m_triangleBuffer = nullptr; }
    if (m_materialSRV) { m_materialSRV->Release(); m_materialSRV = nullptr; }
    if (m_materialBuffer) { m_materialBuffer->Release(); m_materialBuffer = nullptr; }
    if (m_lightSRV) { m_lightSRV->Release(); m_lightSRV = nullptr; }
    if (m_lightBuffer) { m_lightBuffer->Release(); m_lightBuffer = nullptr; }
    if (m_bvhSRV) { m_bvhSRV->Release(); m_bvhSRV = nullptr; }
    if (m_bvhBuffer) { m_bvhBuffer->Release(); m_bvhBuffer = nullptr; }
    if (m_bvhTriangleIndicesSRV) { m_bvhTriangleIndicesSRV->Release(); m_bvhTriangleIndicesSRV = nullptr; }
    if (m_bvhTriangleIndicesBuffer) { m_bvhTriangleIndicesBuffer->Release(); m_bvhTriangleIndicesBuffer = nullptr; }
}

bool Renderer::UploadSceneData(const Scene& scene) {
    CleanupSceneBuffers();
    
    HRESULT hr;
    
    // Prepare triangle data (must match HLSL struct Triangle layout exactly)
    struct GPUTriangle {
        float v0[3]; float _pad_v0;
        float v1[3]; float _pad_v1;
        float v2[3]; float _pad_v2;
        float n0[3]; float _pad_n0;
        float n1[3]; float _pad_n1;
        float n2[3]; float _pad_n2;
        uint32_t materialIndex;
        uint32_t _pad_mat[3];
    }; // Total size: 112 bytes
    
    std::vector<GPUTriangle> triangles;
    for (const auto& mesh : scene.GetMeshes()) {
        const auto& vertices = mesh->GetVertices();
        const auto& indices = mesh->GetIndices();
        uint32_t matIdx = mesh->GetMaterialIndex();
        
        for (size_t i = 0; i < indices.size(); i += 3) {
            GPUTriangle tri;
            const auto& v0 = vertices[indices[i]];
            const auto& v1 = vertices[indices[i + 1]];
            const auto& v2 = vertices[indices[i + 2]];
            
            tri.v0[0] = v0.position.x; tri.v0[1] = v0.position.y; tri.v0[2] = v0.position.z; tri._pad_v0 = 0.0f;
            tri.v1[0] = v1.position.x; tri.v1[1] = v1.position.y; tri.v1[2] = v1.position.z; tri._pad_v1 = 0.0f;
            tri.v2[0] = v2.position.x; tri.v2[1] = v2.position.y; tri.v2[2] = v2.position.z; tri._pad_v2 = 0.0f;
            tri.n0[0] = v0.normal.x; tri.n0[1] = v0.normal.y; tri.n0[2] = v0.normal.z; tri._pad_n0 = 0.0f;
            tri.n1[0] = v1.normal.x; tri.n1[1] = v1.normal.y; tri.n1[2] = v1.normal.z; tri._pad_n1 = 0.0f;
            tri.n2[0] = v2.normal.x; tri.n2[1] = v2.normal.y; tri.n2[2] = v2.normal.z; tri._pad_n2 = 0.0f;
            tri.materialIndex = matIdx;
            tri._pad_mat[0] = tri._pad_mat[1] = tri._pad_mat[2] = 0;
            
            triangles.push_back(tri);
        }
    }
    
    m_numTriangles = static_cast<uint32_t>(triangles.size());
    std::cout << "Uploading " << m_numTriangles << " triangles..." << std::endl;
    
    // Check buffer size limits
    size_t triangleBufferSize = sizeof(GPUTriangle) * m_numTriangles;
    std::cout << "Triangle buffer size: " << triangleBufferSize << " bytes (" 
              << (triangleBufferSize / 1024.0 / 1024.0) << " MB)" << std::endl;
    
    if (triangleBufferSize > 128 * 1024 * 1024) {
        std::cerr << "WARNING: Triangle buffer exceeds 128MB, may fail on some GPUs!" << std::endl;
    }
    
    // Debug: Print first triangle
    if (m_numTriangles > 0) {
        const auto& tri0 = triangles[0];
        std::cout << "  Triangle[0]: v0=[" << tri0.v0[0] << "," << tri0.v0[1] << "," << tri0.v0[2] << "]"
                  << " v1=[" << tri0.v1[0] << "," << tri0.v1[1] << "," << tri0.v1[2] << "]"
                  << " v2=[" << tri0.v2[0] << "," << tri0.v2[1] << "," << tri0.v2[2] << "]"
                  << " matIdx=" << tri0.materialIndex << std::endl;
    }
    
    if (m_numTriangles > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPUTriangle) * m_numTriangles;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPUTriangle);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = triangles.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_triangleBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create triangle buffer! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            std::cerr << "Buffer size: " << bufferDesc.ByteWidth << " bytes" << std::endl;
            std::cerr << "Number of triangles: " << m_numTriangles << std::endl;
            return false;
        }
        std::cout << "Triangle buffer created successfully" << std::endl;
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numTriangles;
        
        hr = m_device->CreateShaderResourceView(m_triangleBuffer, &srvDesc, &m_triangleSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create triangle SRV" << std::endl;
            return false;
        }
    }
    
    // Prepare material data
    struct GPUMaterial {
        uint32_t type;
        float albedo[3];
        float emission[3];
        float metallic;
        float roughness;
        float ior;
        float transmission;
        uint32_t padding[1];
    };
    
    std::vector<GPUMaterial> materials;
    for (const auto& mat : scene.GetMaterials()) {
        GPUMaterial gmat;
        gmat.type = static_cast<uint32_t>(mat->GetType());
        
        glm::vec3 albedo = mat->GetAlbedo();
        gmat.albedo[0] = albedo.x; gmat.albedo[1] = albedo.y; gmat.albedo[2] = albedo.z;
        
        glm::vec3 emission = mat->GetEmission();
        gmat.emission[0] = emission.x; gmat.emission[1] = emission.y; gmat.emission[2] = emission.z;
        
        gmat.metallic = mat->GetMetallic();
        gmat.roughness = mat->GetRoughness();
        gmat.ior = mat->GetIOR();
        gmat.transmission = mat->GetTransmission();
        
        // Debug output
        if (glm::length(emission) > 0.01f) {
            std::cout << "  GPU Material type=" << gmat.type 
                     << " emission=[" << emission.x << "," << emission.y << "," << emission.z << "]" << std::endl;
        }
        
        materials.push_back(gmat);
    }
    
    m_numMaterials = static_cast<uint32_t>(materials.size());
    std::cout << "Uploading " << m_numMaterials << " materials..." << std::endl;
    
    // Check material buffer size
    size_t materialBufferSize = sizeof(GPUMaterial) * m_numMaterials;
    std::cout << "Material buffer size: " << materialBufferSize << " bytes" << std::endl;
    
    if (m_numMaterials > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPUMaterial) * m_numMaterials;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPUMaterial);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = materials.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_materialBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create material buffer! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            std::cerr << "Buffer size: " << bufferDesc.ByteWidth << " bytes" << std::endl;
            std::cerr << "Number of materials: " << m_numMaterials << std::endl;
            return false;
        }
        std::cout << "Material buffer created successfully" << std::endl;
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numMaterials;
        
        hr = m_device->CreateShaderResourceView(m_materialBuffer, &srvDesc, &m_materialSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create material SRV" << std::endl;
            return false;
        }
    }
    
    // Build and upload BVH
    std::cout << "Building BVH for acceleration..." << std::endl;
    BVH bvh;
    
    // Collect all vertices and indices
    std::vector<glm::vec3> allVertices;
    std::vector<uint32_t> allIndices;
    
    for (const auto& mesh : scene.GetMeshes()) {
        const auto& vertices = mesh->GetVertices();
        const auto& indices = mesh->GetIndices();
        
        uint32_t baseVertex = static_cast<uint32_t>(allVertices.size());
        for (const auto& v : vertices) {
            allVertices.push_back(v.position);
        }
        for (uint32_t idx : indices) {
            allIndices.push_back(baseVertex + idx);
        }
    }
    
    auto buildStart = std::chrono::high_resolution_clock::now();
    bvh.Build(allVertices, allIndices);
    auto buildEnd = std::chrono::high_resolution_clock::now();
    auto buildTime = std::chrono::duration_cast<std::chrono::milliseconds>(buildEnd - buildStart).count();
    
    std::cout << "BVH built in " << buildTime << " ms" << std::endl;
    std::cout << "BVH nodes: " << bvh.GetNodeCount() << ", max depth: " << bvh.GetMaxDepth() << std::endl;
    
    // Upload BVH to GPU
    const auto& bvhNodes = bvh.GetNodes();
    m_numBVHNodes = static_cast<uint32_t>(bvhNodes.size());
    
    if (m_numBVHNodes > 0) {
        // First, build a triangle index remapping array
        // This maps BVH internal triangle indices to original GPU buffer indices
        std::vector<int32_t> triangleIndexRemap;
        
        // Convert BVH nodes to GPU format
        struct GPUBVHNode {
            float bboxMin[3]; float _pad0;
            float bboxMax[3]; float _pad1;
            int32_t leftChild;
            int32_t rightChild;
            int32_t firstPrim;
            int32_t primCount;
        };
        
        std::vector<GPUBVHNode> gpuNodes;
        for (const auto& node : bvhNodes) {
            GPUBVHNode gpuNode;
            gpuNode.bboxMin[0] = node.bbox.min.x;
            gpuNode.bboxMin[1] = node.bbox.min.y;
            gpuNode.bboxMin[2] = node.bbox.min.z;
            gpuNode._pad0 = 0.0f;
            gpuNode.bboxMax[0] = node.bbox.max.x;
            gpuNode.bboxMax[1] = node.bbox.max.y;
            gpuNode.bboxMax[2] = node.bbox.max.z;
            gpuNode._pad1 = 0.0f;
            gpuNode.leftChild = node.leftChild;
            gpuNode.rightChild = node.rightChild;
            
            // For leaf nodes, remap triangle indices from BVH internal indices
            // to original GPU buffer indices
            if (node.leftChild == -1) {  // Leaf node
                gpuNode.firstPrim = static_cast<int32_t>(triangleIndexRemap.size());
                gpuNode.primCount = node.primCount;
                
                // Add the original triangle indices to the remap array
                for (int i = 0; i < node.primCount; ++i) {
                    int bvhTriIdx = node.firstPrim + i;
                    int originalIdx = bvh.GetTriangleOriginalIndex(bvhTriIdx);
                    triangleIndexRemap.push_back(originalIdx);
                }
            } else {  // Internal node
                gpuNode.firstPrim = 0;
                gpuNode.primCount = 0;
            }
            
            gpuNodes.push_back(gpuNode);
        }
        
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPUBVHNode) * m_numBVHNodes;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPUBVHNode);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = gpuNodes.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_bvhBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create BVH buffer! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            return false;
        }
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numBVHNodes;
        
        hr = m_device->CreateShaderResourceView(m_bvhBuffer, &srvDesc, &m_bvhSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create BVH SRV" << std::endl;
            return false;
        }
        
        std::cout << "BVH uploaded to GPU (" << (bufferDesc.ByteWidth / 1024.0) << " KB)" << std::endl;
        
        // Upload triangle index remapping buffer
        m_numBVHTriangleIndices = static_cast<uint32_t>(triangleIndexRemap.size());
        
        if (m_numBVHTriangleIndices > 0) {
            D3D11_BUFFER_DESC indexBufferDesc = {};
            indexBufferDesc.ByteWidth = sizeof(int32_t) * m_numBVHTriangleIndices;
            indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
            indexBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            indexBufferDesc.StructureByteStride = sizeof(int32_t);
            indexBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            
            D3D11_SUBRESOURCE_DATA indexInitData = {};
            indexInitData.pSysMem = triangleIndexRemap.data();
            
            hr = m_device->CreateBuffer(&indexBufferDesc, &indexInitData, &m_bvhTriangleIndicesBuffer);
            if (FAILED(hr)) {
                std::cerr << "Failed to create BVH triangle indices buffer!" << std::endl;
                return false;
            }
            
            D3D11_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
            indexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
            indexSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            indexSrvDesc.Buffer.FirstElement = 0;
            indexSrvDesc.Buffer.NumElements = m_numBVHTriangleIndices;
            
            hr = m_device->CreateShaderResourceView(m_bvhTriangleIndicesBuffer, &indexSrvDesc, &m_bvhTriangleIndicesSRV);
            if (FAILED(hr)) {
                std::cerr << "Failed to create BVH triangle indices SRV!" << std::endl;
                return false;
            }
            
            std::cout << "BVH triangle indices uploaded: " << m_numBVHTriangleIndices << " indices (" 
                     << (indexBufferDesc.ByteWidth / 1024.0) << " KB)" << std::endl;
        }
    }
    
    // Prepare light data (placeholder for now)
    struct GPULight {
        uint32_t type;
        float color[3];
        float intensity;
        float position[3];
        float direction[3];
        float size[2];
        uint32_t padding[2];
    };
    
    std::vector<GPULight> lights;
    for (const auto& light : scene.GetLights()) {
        GPULight glight;
        glight.type = static_cast<uint32_t>(light->GetType());
        
        glm::vec3 color = light->GetColor();
        glight.color[0] = color.x; glight.color[1] = color.y; glight.color[2] = color.z;
        glight.intensity = light->GetIntensity();
        
        // Type-specific data would go here
        glight.position[0] = glight.position[1] = glight.position[2] = 0.0f;
        glight.direction[0] = glight.direction[1] = glight.direction[2] = 0.0f;
        glight.size[0] = glight.size[1] = 0.0f;
        
        lights.push_back(glight);
    }
    
    m_numLights = static_cast<uint32_t>(lights.size());
    
    if (m_numLights > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPULight) * m_numLights;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPULight);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = lights.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_lightBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create light buffer" << std::endl;
            return false;
        }
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numLights;
        
        hr = m_device->CreateShaderResourceView(m_lightBuffer, &srvDesc, &m_lightSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create light SRV" << std::endl;
            return false;
        }
    }
    
    std::cout << "Scene data uploaded successfully" << std::endl;
    return true;
}

void Renderer::Cleanup() {
    CleanupSceneBuffers();
    
    if (m_constantBuffer) {
        m_constantBuffer->Release();
        m_constantBuffer = nullptr;
    }
    
    // Release ping-pong accumulation buffers
    for (int i = 0; i < 2; ++i) {
        if (m_accumulationSRV[i]) {
            m_accumulationSRV[i]->Release();
            m_accumulationSRV[i] = nullptr;
        }
        if (m_accumulationUAV[i]) {
            m_accumulationUAV[i]->Release();
            m_accumulationUAV[i] = nullptr;
        }
        if (m_accumulationTexture[i]) {
            m_accumulationTexture[i]->Release();
            m_accumulationTexture[i] = nullptr;
        }
    }
    
    if (m_renderTargetUAV) {
        m_renderTargetUAV->Release();
        m_renderTargetUAV = nullptr;
    }
    
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
    
    if (m_accumShader) {
        m_accumShader->Release();
        m_accumShader = nullptr;
    }
    
    if (m_pathTracingShader) {
        m_pathTracingShader->Release();
        m_pathTracingShader = nullptr;
    }
    
    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    
    if (m_context) {
        m_context->Release();
        m_context = nullptr;
    }
    
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    
    std::cout << "Renderer cleaned up" << std::endl;
}

} // namespace ACG
