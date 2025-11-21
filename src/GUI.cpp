#include "GUI.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "imgui.h"
#include <commdlg.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/rotate_vector.hpp>

namespace GUI {

// Internal state for async rendering
static std::atomic<bool> g_isRendering(false);
static std::atomic<bool> g_renderComplete(false);
static std::atomic<int> g_currentSample(0);
static std::atomic<int> g_totalSamples(0);
static std::string g_renderResultMessage;
static std::mutex g_renderMutex;
static std::unique_ptr<std::thread> g_renderThread;

// Scene loading tracking
static std::string g_lastLoadedScene;
static std::string g_lastLoadedEnvMap;

void InitializeGUIState(GUIState& state, const std::string& exeDirectory) {
    // Set default output path
    if (!exeDirectory.empty()) {
        std::string defaultOutput = exeDirectory + "\\output.ppm";
        strncpy_s(state.outputPath, defaultOutput.c_str(), sizeof(state.outputPath) - 1);
        state.outputPathInitialized = true;
    }
}

void ShutdownGUI() {
    if (g_renderThread && g_renderThread->joinable()) {
        g_renderThread->join();
    }
}

// Load PPM image and create D3D12 texture for display
bool LoadPPMToTexture(const std::string& ppmPath, ACG::Renderer* renderer, GUIState& state) {
    std::ifstream file(ppmPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open PPM file: " << ppmPath << std::endl;
        return false;
    }

    std::string magic;
    int width, height, maxval;
    file >> magic >> width >> height >> maxval;
    file.get(); // Skip whitespace

    if (magic != "P6") {
        std::cerr << "Only P6 PPM format is supported" << std::endl;
        return false;
    }

    // Read pixel data (RGB)
    std::vector<uint8_t> pixels(width * height * 3);
    file.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
    file.close();

    // Convert RGB to RGBA
    std::vector<uint8_t> rgbaPixels(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        rgbaPixels[i * 4 + 0] = pixels[i * 3 + 0]; // R
        rgbaPixels[i * 4 + 1] = pixels[i * 3 + 1]; // G
        rgbaPixels[i * 4 + 2] = pixels[i * 3 + 2]; // B
        rgbaPixels[i * 4 + 3] = 255;                // A
    }

    // Clean up old resources
    if (state.renderResultTexture) {
        state.renderResultTexture->Release();
        state.renderResultTexture = nullptr;
    }
    if (state.renderResultSRVHeap) {
        state.renderResultSRVHeap->Release();
        state.renderResultSRVHeap = nullptr;
    }

    ID3D12Device* device = renderer->GetDevice();

    // Create texture resource
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&state.renderResultTexture)
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create texture resource" << std::endl;
        return false;
    }

    // Create upload buffer
    UINT64 uploadBufferSize = GetRequiredIntermediateSize(state.renderResultTexture, 0, 1);
    
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* uploadBuffer = nullptr;
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create upload buffer" << std::endl;
        state.renderResultTexture->Release();
        state.renderResultTexture = nullptr;
        return false;
    }

    // Create command list for upload
    ID3D12CommandAllocator* cmdAllocator = nullptr;
    ID3D12GraphicsCommandList* cmdList = nullptr;
    
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocator, nullptr, IID_PPV_ARGS(&cmdList));

    // Upload texture data
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = rgbaPixels.data();
    subresourceData.RowPitch = width * 4;
    subresourceData.SlicePitch = subresourceData.RowPitch * height;

    UpdateSubresources(cmdList, state.renderResultTexture, uploadBuffer, 0, 0, 1, &subresourceData);

    // Transition to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = state.renderResultTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->Close();

    // Execute command list
    ID3D12CommandQueue* cmdQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    
    ID3D12CommandList* cmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);

    // Wait for upload to complete
    ID3D12Fence* fence = nullptr;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    cmdQueue->Signal(fence, 1);
    fence->SetEventOnCompletion(1, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
    CloseHandle(fenceEvent);

    // Clean up
    fence->Release();
    cmdQueue->Release();
    cmdList->Release();
    cmdAllocator->Release();
    uploadBuffer->Release();

    // Create SRV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&state.renderResultSRVHeap));
    if (FAILED(hr)) {
        std::cerr << "Failed to create SRV heap" << std::endl;
        state.renderResultTexture->Release();
        state.renderResultTexture = nullptr;
        return false;
    }

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(state.renderResultTexture, &srvDesc, 
        state.renderResultSRVHeap->GetCPUDescriptorHandleForHeapStart());

    std::cout << "Successfully loaded PPM image: " << width << "x" << height << std::endl;
    return true;
}

std::string OpenFileDialog(HWND hwnd, const char* filter, const char* title) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

std::string SaveFileDialog(HWND hwnd, const char* filter, const char* title) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    
    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
}

void RenderSettingsWindow(ACG::Renderer* renderer, GUIState& state, HWND hwnd) {
    ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("Output Resolution");
    ImGui::InputInt("Width", &state.width);
    ImGui::InputInt("Height", &state.height);
    
    ImGui::Separator();
    ImGui::Text("Sampling");
    if (ImGui::InputInt("Samples Per Pixel", &state.samplesPerPixel)) {
        if (state.samplesPerPixel < 1) state.samplesPerPixel = 1;
    }
    
    if (ImGui::InputInt("Max Bounces", &state.maxBounces)) {
        if (state.maxBounces < 1) state.maxBounces = 1;
        renderer->SetMaxBounces(state.maxBounces);
    }
    
    ImGui::Separator();
    ImGui::Text("Lighting");
    
    // Environment Light Section
    if (ImGui::TreeNode("Environment Light")) {
        ImGui::InputText("Environment Map", state.envMapPath, sizeof(state.envMapPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##EnvMap")) {
            std::string path = OpenFileDialog(hwnd, 
                "HDR/EXR Images\0*.hdr;*.exr\0All Files\0*.*\0\0",
                "Select Environment Map");
            if (!path.empty()) {
                strncpy_s(state.envMapPath, path.c_str(), sizeof(state.envMapPath) - 1);
            }
        }
        
        if (ImGui::SliderFloat("Intensity##EnvLight", &state.envLightIntensity, 0.0f, 10.0f)) {
            renderer->SetEnvironmentLightIntensity(state.envLightIntensity);
        }
        
        ImGui::TreePop();
    }
    
    // Directional Sun Light Section
    if (ImGui::TreeNode("Directional Sun Light")) {
        if (ImGui::SliderFloat("Intensity##SunLight", &state.sunIntensity, 0.0f, 20.0f)) {
            renderer->SetSunIntensity(state.sunIntensity);
            renderer->ResetAccumulation();
        }
        
        bool sunDirChanged = false;
        if (ImGui::SliderFloat("Azimuth (deg)", &state.sunAzimuth, 0.0f, 360.0f)) sunDirChanged = true;
        if (ImGui::SliderFloat("Elevation (deg)", &state.sunElevation, -89.9f, 89.9f)) sunDirChanged = true;
        if (sunDirChanged) {
            // Convert spherical angles to direction vector (Y-up)
            float az = glm::radians(state.sunAzimuth);
            float el = glm::radians(state.sunElevation);
            glm::vec3 dir = glm::vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
            renderer->SetSunDirection(dir);
            renderer->ResetAccumulation();
        }
        
        if (ImGui::ColorEdit3("Color##SunLight", state.sunColor)) {
            renderer->SetSunColor(glm::vec3(state.sunColor[0], state.sunColor[1], state.sunColor[2]));
            renderer->ResetAccumulation();
        }
        
        ImGui::TreePop();
    }
    
    ImGui::Separator();
    ImGui::Text("Scene");
    ImGui::InputText("Model Path", state.modelPath, sizeof(state.modelPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        std::string path = OpenFileDialog(hwnd, 
            "3D Models\0*.obj;*.fbx;*.gltf\0All Files\0*.*\0\0",
            "Select 3D Model");
        if (!path.empty()) {
            strncpy_s(state.modelPath, path.c_str(), sizeof(state.modelPath) - 1);
        }
    }
    
    // Python loader now handles all formats automatically
    
    // Batch loading configuration (deprecated - kept for UI state)
    ImGui::Separator();
    ImGui::Text("Scene Loading");
    ImGui::Checkbox("Enable Batch Loading", &state.enableBatchLoading);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Load large scenes in batches to avoid memory overflow.\nRecommended for scenes with >500 meshes or >1M triangles.");
    }
    
    if (state.enableBatchLoading) {
        ImGui::PushItemWidth(150);
        ImGui::SliderInt("Meshes/Batch", &state.maxMeshesPerBatch, 100, 2000);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Number of meshes to process in each batch.\nLower = less memory, slower loading");
        }
        
        ImGui::SliderInt("Textures/Batch", &state.maxTexturesPerBatch, 16, 128);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maximum textures to load per batch");
        }
        
        ImGui::SliderInt("Memory Limit (MB)", &state.maxMemoryMB, 1024, 8192);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Estimated memory usage limit (warning only)");
        }
        ImGui::PopItemWidth();
    }
    ImGui::Separator();
    
    ImGui::InputText("Output Path", state.outputPath, sizeof(state.outputPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse##Output")) {
        std::string path = SaveFileDialog(hwnd,
            "PPM Image\0*.ppm\0All Files\0*.*\0\0",
            "Save Output Image");
        if (!path.empty()) {
            strncpy_s(state.outputPath, path.c_str(), sizeof(state.outputPath) - 1);
        }
    }
    
    ImGui::Checkbox("Auto-render on load", &state.autoRenderOnLoad);
    
    ImGui::End();
}

void RenderControlsWindow(ACG::Renderer* renderer, GUIState& state) {
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    // Check if render completed
    if (g_renderComplete.load()) {
        g_renderComplete.store(false);
        g_isRendering.store(false);
        std::lock_guard<std::mutex> lock(g_renderMutex);
        state.renderStatus = g_renderResultMessage;
        state.showRenderStatus = true;
        // Join the thread
        if (g_renderThread && g_renderThread->joinable()) {
            g_renderThread->join();
            g_renderThread.reset();
        }
    }
    
    bool canStartRender = !g_isRendering.load();
    if (!canStartRender) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }
    
    // Show Start or Stop button based on rendering state
    if (!g_isRendering.load()) {
        if (ImGui::Button("Start Render", ImVec2(150, 30)) && canStartRender) {
            if (strlen(state.modelPath) > 0 && strlen(state.outputPath) > 0) {
                // Copy parameters
                std::string modelPathStr = state.modelPath;
                std::string outputPathStr = state.outputPath;
                std::string envMapPathStr = state.envMapPath;
                int samples = state.samplesPerPixel;
                int bounces = state.maxBounces;
                int renderWidth = state.width;
                int renderHeight = state.height;
                
                // Join previous thread if exists
                if (g_renderThread && g_renderThread->joinable()) {
                    g_renderThread->join();
                }
                
                // Wait for previous render thread to finish
                if (g_renderThread && g_renderThread->joinable()) {
                    g_renderThread->join();
                    g_renderThread.reset();
                }
                
                std::cout << "Starting async load and render..." << std::endl;
                std::cout.flush();
                
                // Check if scene needs to be loaded
                bool needsSceneLoad = (g_lastLoadedScene != modelPathStr);
                // Environment map needs update if: path changed (including empty->non-empty or non-empty->empty)
                bool needsEnvMapLoad = (g_lastLoadedEnvMap != envMapPathStr);
                
                // Record start time
                auto startTime = std::chrono::steady_clock::now();
                
                g_renderThread = std::make_unique<std::thread>([renderer, modelPathStr, outputPathStr, envMapPathStr, samples, bounces, renderWidth, renderHeight, needsSceneLoad, needsEnvMapLoad, startTime, &state]() {
                    try {
                        // Set rendering flags at the start
                        g_isRendering.store(true);
                        g_renderComplete.store(false);
                        g_totalSamples.store(samples);
                        g_currentSample.store(0);
                        
                        // Only load scene if it hasn't been loaded yet or path changed
                        if (needsSceneLoad) {
                            std::cout << "[Async] Loading scene: " << modelPathStr << std::endl;
                            std::cout.flush();
                            
                            auto sceneLoadStart = std::chrono::steady_clock::now();
                            
                            // Use Python loader (automatically handles all formats)
                            renderer->LoadSceneAsync(modelPathStr);
                            
                            auto sceneLoadEnd = std::chrono::steady_clock::now();
                            auto sceneLoadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(sceneLoadEnd - sceneLoadStart);
                            state.modelLoadTime = sceneLoadDuration.count() / 1000.0f;
                        } else {
                            std::cout << "[Async] Using already loaded scene" << std::endl;
                            std::cout.flush();
                        }
                        
                        // Handle environment map
                        if (envMapPathStr.empty()) {
                            // Clear environment map if path is empty
                            if (!g_lastLoadedEnvMap.empty()) {
                                std::cout << "[Async] Clearing environment map" << std::endl;
                                std::cout.flush();
                                try {
                                    renderer->ClearEnvironmentMap();
                                    g_lastLoadedEnvMap.clear();
                                    std::cout << "[Async] Environment map cleared" << std::endl;
                                } catch (const std::exception& e) {
                                    std::cerr << "[Async] Failed to clear environment map: " << e.what() << std::endl;
                                }
                            }
                        } else if (needsEnvMapLoad) {
                            // Load new environment map
                            std::cout << "[Async] Loading environment map: " << envMapPathStr << std::endl;
                            std::cout.flush();
                            try {
                                renderer->SetEnvironmentMap(envMapPathStr);
                                g_lastLoadedEnvMap = envMapPathStr;
                                std::cout << "[Async] Environment map loaded successfully" << std::endl;
                            } catch (const std::exception& e) {
                                std::cerr << "[Async] Failed to load environment map: " << e.what() << " - clearing environment map" << std::endl;
                                renderer->ClearEnvironmentMap();
                                g_lastLoadedEnvMap.clear();
                            }
                        } else {
                            std::cout << "[Async] Using already loaded environment map" << std::endl;
                        }
                        
                        std::cout << "[Async] Starting render..." << std::endl;
                        std::cout.flush();
                        
                        // Resize if needed
                        renderer->OnResize(renderWidth, renderHeight);
                        
                        // Start progress monitoring thread
                        std::atomic<bool> progressThreadRunning(true);
                        auto progressThread = std::thread([renderer, samples, &progressThreadRunning]() {
                            while (progressThreadRunning.load()) {
                                int currentSamples = renderer->GetAccumulatedSamples();
                                g_currentSample.store(currentSamples);
                                if (currentSamples >= samples) {
                                    break;  // Rendering complete
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                        });
                        
                        renderer->RenderToFile(outputPathStr, samples, bounces);
                        
                        // Signal progress thread to stop and wait for it
                        progressThreadRunning.store(false);
                        if (progressThread.joinable()) {
                            progressThread.join();
                        }
                        
                        // Calculate actual render time
                        auto endTime = std::chrono::steady_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
                        state.lastRenderTime = duration.count() / 1000.0f;  // Convert to seconds
                        
                        // Load rendered image for display
                        if (LoadPPMToTexture(outputPathStr, renderer, state)) {
                            std::cout << "Render result loaded for display" << std::endl;
                        }
                        
                        std::lock_guard<std::mutex> lock(g_renderMutex);
                        g_renderResultMessage = "Rendering complete!\nOutput saved to:\n" + outputPathStr;
                        
                        // Update last loaded scene
                        if (needsSceneLoad) {
                            g_lastLoadedScene = modelPathStr;
                        }
                    }
                    catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(g_renderMutex);
                        g_renderResultMessage = std::string("Error: ") + (e.what() ? e.what() : "Unknown error");
                        std::cerr << "[Async] Failed: " << g_renderResultMessage << std::endl;
                        std::cerr.flush();
                    }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(g_renderMutex);
                        g_renderResultMessage = "Unknown exception occurred";
                        std::cerr << "[Async] Failed: Unknown exception" << std::endl;
                        std::cerr.flush();
                    }
                    
                    // Clear rendering flag
                    g_isRendering.store(false);
                    g_renderComplete.store(true);
                });
            } else {
                if (strlen(state.modelPath) == 0) state.renderStatus = "Please select a model file first";
                else state.renderStatus = "Please specify an output path first";
                state.showRenderStatus = true;
                std::cerr << state.renderStatus << std::endl;
            }
        }
    } else {
        // Show Stop Render button while rendering
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Stop Render", ImVec2(150, 30))) {
            std::cout << "Stop render requested by user" << std::endl;
            renderer->StopRender();
            g_isRendering.store(false);  // Update UI state immediately
        }
        ImGui::PopStyleColor();
    }
    
    if (!canStartRender) {
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Rendering in background...");
    }
    
    // Show timing information below Start button
    ImGui::Separator();
    
    if (state.lastRenderTime > 0.0f) {
        ImGui::Text("Last Render Time: %.2f seconds", state.lastRenderTime);
    } else {
        ImGui::Text("Last Render Time: N/A");
    }
    
    if (state.modelLoadTime > 0.0f) {
        ImGui::Text("Model Load Time: %.2f seconds", state.modelLoadTime);
    } else {
        ImGui::Text("Model Load Time: N/A");
    }
    
    // Show rendering progress info
    if (g_isRendering.load()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Rendering offline to file");
        
        // Show actual progress
        int currentSample = g_currentSample.load();
        int totalSamples = g_totalSamples.load();
        if (totalSamples > 0) {
            float progress = static_cast<float>(currentSample) / static_cast<float>(totalSamples);
            char progressText[128];
            sprintf_s(progressText, "Progress: %d / %d samples (%.1f%%)", currentSample, totalSamples, progress * 100.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), progressText);
        } else {
            ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Initializing...");
        }
        
        ImGui::Text("Please wait, the GUI remains responsive");
    }
    
    ImGui::End();
}

void RenderCameraWindow(ACG::Renderer* renderer, GUIState& state) {
    ImGui::Begin("Camera Settings");
        
    ACG::Camera* camera = renderer->GetCamera();
    if (camera) {
        // Initialize camera orbit angles from current camera once
        if (!state.cameraAnglesInitialized) {
            glm::vec3 pos = camera->GetPosition();
            glm::vec3 target = camera->GetTarget();
            glm::vec3 dir = glm::normalize(target - pos);
            // Azimuth: atan2(z, x), Elevation: asin(y)
            state.cameraAzimuth = std::atan2(dir.z, dir.x) * 180.0f / glm::pi<float>();
            state.cameraElevation = std::asin(glm::clamp(dir.y, -1.0f, 1.0f)) * 180.0f / glm::pi<float>();
            state.cameraDistance = glm::length(target - pos);
            // Initialize roll (up angle) relative to local up
            glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 camUp = camera->GetUp();
            glm::vec3 right = glm::normalize(glm::cross(dir, worldUp));
            glm::vec3 localUp = glm::normalize(glm::cross(right, dir));
            float a = glm::dot(localUp, camUp);
            float b = glm::dot(right, camUp);
            state.cameraUpAngle = std::atan2(b, a) * 180.0f / glm::pi<float>();
            state.cameraAnglesInitialized = true;
        }

        ImGui::Text("Angle Controls");
        bool orbitChanged = false;
        if (ImGui::SliderFloat("Azimuth (deg)", &state.cameraAzimuth, 0.0f, 360.0f)) orbitChanged = true;
        if (ImGui::SliderFloat("Elevation (deg)", &state.cameraElevation, -89.9f, 89.9f)) orbitChanged = true;
        if (ImGui::SliderFloat("Roll (deg)", &state.cameraUpAngle, -180.0f, 180.0f)) orbitChanged = true;
        if (orbitChanged) {
            // Convert spherical angles to world-space direction (Y-up)
            float az = state.cameraAzimuth * glm::pi<float>() / 180.0f;
            float el = state.cameraElevation * glm::pi<float>() / 180.0f;
            glm::vec3 dir = glm::vec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
            glm::vec3 pos = camera->GetPosition();
            glm::vec3 target = pos + dir * state.cameraDistance;
            camera->SetTarget(target);
            // Recompute up from roll angle
            glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 baseUp = worldUp - glm::dot(worldUp, dir) * dir;
            if (glm::dot(baseUp, baseUp) < 1e-6f) {
                baseUp = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            baseUp = glm::normalize(baseUp);
            float rollRad = state.cameraUpAngle * glm::pi<float>() / 180.0f;
            glm::vec3 rolledUpCandidate = glm::rotate(baseUp, rollRad, dir);
            glm::vec3 right = glm::normalize(glm::cross(dir, rolledUpCandidate));
            glm::vec3 up = glm::normalize(glm::cross(right, dir));
            camera->SetUp(up);
            renderer->ResetAccumulation();
        }

        ImGui::Text("Camera Position");
        glm::vec3 pos = camera->GetPosition();
        if (ImGui::InputFloat("X##Pos", &pos.x, 0.1f, 1.0f, "%.2f")) {
            camera->SetPosition(pos);
        }
        if (ImGui::InputFloat("Y##Pos", &pos.y, 0.1f, 1.0f, "%.2f")) {
            camera->SetPosition(pos);
        }
        if (ImGui::InputFloat("Z##Pos", &pos.z, 0.1f, 1.0f, "%.2f")) {
            camera->SetPosition(pos);
        }
        ImGui::SameLine();
        ImGui::Text("Position");
        
        
        ImGui::Text("Field of View");
        float fov = camera->GetFOV();
        if (ImGui::InputFloat("FOV (deg)", &fov, 1.0f, 10.0f, "%.1f")) {
            camera->SetFOV(fov);
        }
        
        if (ImGui::Button("Reset to Default")) {
            camera->SetPosition(glm::vec3(0.0f, 1.0f, 3.0f));
            camera->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
            camera->SetUp(glm::vec3(0.0f, 1.0f, 0.0f));
            camera->SetFOV(60.0f);
        }
    }
        
    ImGui::End();
}

void RenderResultWindow(ACG::Renderer* renderer, GUIState& state) {
    ImGui::SetNextWindowSize(ImVec2(850, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Render Result");
    
    // Get available content region
    ImVec2 contentRegion = ImGui::GetContentRegionAvail();
    float windowWidth = contentRegion.x;
    float windowHeight = contentRegion.y;
    
    // Check if rendering
    bool isRendering = g_isRendering.load();
    
    if (isRendering) {
        // Show progress overlay in center
        int currentSample = g_currentSample.load();
        int totalSamples = g_totalSamples.load();
        
        // Create a centered region for progress display
        ImVec2 textSize = ImGui::CalcTextSize("Rendering...");
        ImGui::SetCursorPosX((windowWidth - textSize.x) * 0.5f);
        ImGui::SetCursorPosY(windowHeight * 0.4f);
        ImGui::Text("Rendering...");
        
        if (totalSamples > 0) {
            float progress = static_cast<float>(currentSample) / static_cast<float>(totalSamples);
            char progressText[128];
            sprintf_s(progressText, "%d / %d samples (%.1f%%)", currentSample, totalSamples, progress * 100.0f);
            
            // Center progress bar
            ImVec2 progressBarSize(windowWidth * 0.6f, 0);
            ImGui::SetCursorPosX((windowWidth - progressBarSize.x) * 0.5f);
            ImGui::ProgressBar(progress, progressBarSize, progressText);
        } else {
            ImVec2 progressBarSize(windowWidth * 0.6f, 0);
            ImGui::SetCursorPosX((windowWidth - progressBarSize.x) * 0.5f);
            ImGui::ProgressBar(0.0f, progressBarSize, "Initializing...");
        }
    } else {
        // Show render result if available
        if (state.renderResultTexture && state.renderResultSRVHeap) {
            // Get descriptor handle for ImGui
            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = state.renderResultSRVHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle = state.renderResultSRVHeap->GetGPUDescriptorHandleForHeapStart();
            
            // Display the texture
            ImGui::Image((ImTextureID)srvGPUHandle.ptr, ImVec2(static_cast<float>(state.width), static_cast<float>(state.height)));
        } else {
            // No result available
            ImVec2 textSize = ImGui::CalcTextSize("No render result available");
            ImGui::SetCursorPosX((windowWidth - textSize.x) * 0.5f);
            ImGui::SetCursorPosY(windowHeight * 0.5f);
            ImGui::Text("No render result available");
            
            ImGui::SetCursorPosX((windowWidth - 200) * 0.5f);
            ImGui::TextWrapped("Start a render to see results here");
        }
    }
    
    ImGui::End();
}

void RenderLogWindow(const std::vector<std::string>& logMessages) {
    ImGui::Begin("Log Details");
    
    static bool autoScroll = true;
    
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        // Cannot clear const vector, just skip
    }
    
    ImGui::Separator();
    ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    // 显示实际的日志内容
    for (const auto& msg : logMessages) {
        ImGui::TextUnformatted(msg.c_str());
    }
    
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
    ImGui::End();
}

// Main GUI rendering function
void RenderGUI(ACG::Renderer* renderer, GUIState& state, HWND hwnd) {
    // Initialize environment light on first call
    if (!state.envLightInitialized) {
        renderer->SetEnvironmentLightIntensity(state.envLightIntensity);
        state.envLightInitialized = true;
    }
    
    // Show render status popup
    if (state.showRenderStatus) {
        ImGui::OpenPopup("Render Status");
        state.showRenderStatus = false;
    }
    if (ImGui::BeginPopupModal("Render Status", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", state.renderStatus.c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    RenderSettingsWindow(renderer, state, hwnd);
    RenderControlsWindow(renderer, state);
    RenderCameraWindow(renderer, state);
    RenderResultWindow(renderer, state);
    
    if (state.pLogMessages) {
        RenderLogWindow(*state.pLogMessages);
    }
}

} // namespace GUI
