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
    ImGui::TextDisabled("(Total number of samples to accumulate per pixel)");
    
    if (ImGui::InputInt("Max Bounces", &state.maxBounces)) {
        if (state.maxBounces < 1) state.maxBounces = 1;
        renderer->SetMaxBounces(state.maxBounces);
    }
    
    ImGui::Separator();
    ImGui::Text("Lighting");
    
    // Environment Light Section
    if (ImGui::TreeNode("Environment Light")) {
        ImGui::TextDisabled("(HDR/EXR skybox for indirect lighting)");
        
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
        ImGui::TextDisabled("(Will be loaded automatically on Start Render)");
        
        if (ImGui::SliderFloat("Intensity##EnvLight", &state.envLightIntensity, 0.0f, 10.0f)) {
            renderer->SetEnvironmentLightIntensity(state.envLightIntensity);
        }
        ImGui::TextDisabled("(Multiplier for environment map brightness)");
        
        ImGui::TreePop();
    }
    
    // Directional Sun Light Section
    if (ImGui::TreeNode("Directional Sun Light")) {
        ImGui::TextDisabled("(Distant directional light source)");
        
        if (ImGui::SliderFloat("Intensity##SunLight", &state.sunIntensity, 0.0f, 20.0f)) {
            renderer->SetSunIntensity(state.sunIntensity);
            renderer->ResetAccumulation();
        }
        ImGui::TextDisabled("(Set to 0 to disable sun light)");
        
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
    
    ImGui::Checkbox("Use Custom MTL Parser (OBJ only)", &state.useCustomMTLParser);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable custom MTL parser for accurate material loading from OBJ files.\nDisabled automatically for non-OBJ formats (FBX, GLTF, etc.)");
    }
    
    // Batch loading configuration
    ImGui::Separator();
    ImGui::Text("Batch Loading (for large scenes)");
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
                bool useCustomMTL = state.useCustomMTLParser;
                
                // Batch loading configuration
                bool enableBatch = state.enableBatchLoading;
                int meshesPerBatch = state.maxMeshesPerBatch;
                int texturesPerBatch = state.maxTexturesPerBatch;
                int memoryLimit = state.maxMemoryMB;
                
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
                
                g_renderThread = std::make_unique<std::thread>([renderer, modelPathStr, outputPathStr, envMapPathStr, samples, bounces, renderWidth, renderHeight, needsSceneLoad, needsEnvMapLoad, useCustomMTL, enableBatch, meshesPerBatch, texturesPerBatch, memoryLimit, startTime, &state]() {
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
                            
                            // Configure batch loading
                            ACG::SceneLoadConfig config;
                            config.useCustomMTLParser = useCustomMTL;
                            config.enableBatchLoading = enableBatch;
                            config.maxMeshesPerBatch = meshesPerBatch;
                            config.maxTexturesPerBatch = texturesPerBatch;
                            config.maxMemoryMB = memoryLimit;
                            
                            renderer->LoadSceneAsyncEx(modelPathStr, config);
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

        ImGui::Text("Angle Controls (rotate camera orientation)");
        bool orbitChanged = false;
        if (ImGui::SliderFloat("Azimuth (deg)", &state.cameraAzimuth, 0.0f, 360.0f)) orbitChanged = true;
        if (ImGui::SliderFloat("Elevation (deg)", &state.cameraElevation, -89.9f, 89.9f)) orbitChanged = true;
        ImGui::TextDisabled("(Rotate camera about its own position: change orientation only)");
        if (orbitChanged) {
            // Convert spherical angles to world-space direction (Y-up)
            float az = state.cameraAzimuth * glm::pi<float>() / 180.0f;
            float el = state.cameraElevation * glm::pi<float>() / 180.0f;
            glm::vec3 dir = glm::vec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
            glm::vec3 pos = camera->GetPosition();
            glm::vec3 target = pos + dir * state.cameraDistance; // target positioned in front of camera (internal fixed distance)
            camera->SetTarget(target);
            // Recompute up from roll angle using worldUp projection, rotate and orthonormalize
            glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            // Project worldUp onto plane perpendicular to dir to get a stable base up
            glm::vec3 baseUp = worldUp - glm::dot(worldUp, dir) * dir;
            if (glm::dot(baseUp, baseUp) < 1e-6f) {
                // dir is nearly parallel to worldUp; pick an arbitrary perpendicular
                baseUp = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            baseUp = glm::normalize(baseUp);
            float rollRad = state.cameraUpAngle * glm::pi<float>() / 180.0f;
            glm::vec3 rolledUpCandidate = glm::rotate(baseUp, rollRad, dir);
            // Orthonormalize: right, up
            glm::vec3 right = glm::normalize(glm::cross(dir, rolledUpCandidate));
            glm::vec3 up = glm::normalize(glm::cross(right, dir));
            camera->SetUp(up);
            renderer->ResetAccumulation();
        }

        // Up angle control (roll)
        ImGui::Separator();
        ImGui::Text("Up Vector Angle (Roll)");
        if (ImGui::SliderFloat("Up Angle (deg)", &state.cameraUpAngle, -180.0f, 180.0f)) {
            // Apply roll to current direction using same robust method as above
            glm::vec3 pos = camera->GetPosition();
            glm::vec3 dir = glm::normalize(camera->GetTarget() - pos);
            glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 baseUp = worldUp - glm::dot(worldUp, dir) * dir;
            if (glm::dot(baseUp, baseUp) < 1e-6f) baseUp = glm::vec3(1.0f, 0.0f, 0.0f);
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

void RenderStatisticsWindow(ACG::Renderer* renderer, GUIState& state) {
    ImGui::Begin("Renderer Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    const char* statusText = g_isRendering.load() ? "Rendering" : "Idle";
    ImGui::Text("Status: %s", statusText);
    
    // Show rendering progress
    if (g_isRendering.load()) {
        int currentSample = g_currentSample.load();
        int totalSamples = g_totalSamples.load();
        if (totalSamples > 0) {
            float progress = static_cast<float>(currentSample) / static_cast<float>(totalSamples);
            char progressText[128];
            sprintf_s(progressText, "%d / %d samples (%.1f%%)", currentSample, totalSamples, progress * 100.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), progressText);
        } else {
            ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Initializing...");
        }
    } else {
        ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Idle");
    }
    
    // Display render time
    if (g_isRendering.load()) {
        // Note: We don't have easy access to start time here unless we store it in state
        // For now, just show "in progress"
        ImGui::Text("Render Time: (in progress)");
    } else if (state.lastRenderTime > 0.0f) {
        ImGui::Text("Render Time: %.1f seconds (last render)", state.lastRenderTime);
    } else {
        ImGui::Text("Render Time: N/A");
    }
    
    ImGui::Separator();
    ImGui::Text("Scene Info:");
    ACG::Scene* scene = renderer->GetScene();
    if (scene) {
        ImGui::Text("  Meshes: %zu", scene->GetMeshes().size());
        ImGui::Text("  Materials: %zu", scene->GetMaterials().size());
        ImGui::Text("  Lights: %zu", scene->GetLights().size());
    } else {
        ImGui::Text("  No scene loaded");
    }
    
    ImGui::End();
}

void RenderLogWindow(const std::vector<std::string>& logMessages) {
    ImGui::Begin("Log Details");
    
    static bool autoScroll = true;
    // Note: We can't clear the external log vector easily without a pointer to it
    // But we can just show it
    
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    
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
    RenderStatisticsWindow(renderer, state);
    
    if (state.pLogMessages) {
        RenderLogWindow(*state.pLogMessages);
    }
}

} // namespace GUI
