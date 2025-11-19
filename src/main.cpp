#define WIN32_LEAN_AND_MEAN
#define UNICODE
#include <Windows.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <codecvt>
#include <locale>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "LogRedirector.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <glm/glm.hpp>
#include <commdlg.h>

// Global variable for executable directory
std::string g_exeDirectory;
static HWND g_hwnd = nullptr;

// Async rendering state
static std::atomic<bool> g_isRendering(false);
static std::atomic<bool> g_renderComplete(false);
static std::atomic<int> g_currentSample(0);     // Current sample being rendered
static std::atomic<int> g_totalSamples(0);      // Total samples to render
static std::string g_renderResultMessage;
static std::mutex g_renderMutex;
static std::unique_ptr<std::thread> g_renderThread;

// 文件对话框辅助函数
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

// Forward declare the renderer pointer
ACG::Renderer* g_pRenderer = nullptr;
bool g_ImGuiInitialized = false;

// Scene loading tracking
std::string g_lastLoadedScene;
std::string g_lastLoadedEnvMap;

// 日志系统
std::vector<std::string> g_logMessages;
std::ofstream g_logFile;
ACG::LogRedirector* g_coutRedirector = nullptr;
ACG::LogRedirector* g_cerrRedirector = nullptr;

void AddLogMessage(const std::string& msg) {
    g_logMessages.push_back(msg);
    if (g_logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t);
        char timeStr[100];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        g_logFile << "[" << timeStr << "] " << msg << std::endl;
        g_logFile.flush();
    }
}

// Forward declarations
void RenderGUI(ACG::Renderer* renderer);

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Handle ImGui events first (only if initialized)
    if (g_ImGuiInitialized && ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;
        
    switch (uMsg) {
        case WM_CREATE:
        {
            // Store the renderer pointer passed from CreateWindowEx
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
            g_pRenderer = reinterpret_cast<ACG::Renderer*>(pCreateStruct->lpCreateParams);
        }
        return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_SIZE:
            if (g_pRenderer && wParam != SIZE_MINIMIZED) {
                g_pRenderer->OnResize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Get executable directory for default output path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exePathWStr(exePath);
    size_t lastSlash = exePathWStr.find_last_of(L"\\/");
    std::wstring exeDirWStr = exePathWStr.substr(0, lastSlash);
    
    // Convert to UTF-8 string
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, exeDirWStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string exeDirStr(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, exeDirWStr.c_str(), -1, &exeDirStr[0], sizeNeeded, nullptr, nullptr);
    exeDirStr.pop_back(); // Remove null terminator
    g_exeDirectory = exeDirStr;
    
    const wchar_t CLASS_NAME[] = L"ACG DXR Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    if (!RegisterClass(&wc)) {
        MessageBox(nullptr, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ACG::Renderer renderer(1280, 720);

    RECT windowRect = { 0, 0, 1280, 720 };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"ACG Project - DirectX 12",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        &renderer
    );

    if (hwnd == nullptr) {
        MessageBox(nullptr, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    try {
        // 打开日志文件
        g_logFile.open("renderer.log", std::ios::out | std::ios::trunc);
        if (g_logFile.is_open()) {
            AddLogMessage("Log file opened successfully");
        }
        
        // 设置日志重定向（同时重定向 cout 和 cerr）
        g_coutRedirector = new ACG::LogRedirector(std::cout, AddLogMessage);
        g_cerrRedirector = new ACG::LogRedirector(std::cerr, AddLogMessage);
        
        // 添加控制台窗口用于调试
        // AllocConsole();
        // FILE* fp;
        // freopen_s(&fp, "CONOUT$", "w", stdout);
        // freopen_s(&fp, "CONOUT$", "w", stderr);
        
        std::cout << "Initializing renderer..." << std::endl;
        renderer.OnInit(hwnd);
        std::cout << "Renderer initialized successfully" << std::endl;
        
        // 初始化 ImGui
        std::cout << "Initializing ImGui..." << std::endl;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Set ImGui ini file path to executable directory
        static std::string iniPath = g_exeDirectory + "\\imgui.ini";
        io.IniFilename = iniPath.c_str();
        
        ImGui::StyleColorsDark();
        
        // 初始化 ImGui Win32 后端
        ImGui_ImplWin32_Init(hwnd);
        
        // 初始化 ImGui DX12 后端
        // 需要从 Renderer 获取 DX12 资源
        ImGui_ImplDX12_Init(
            renderer.GetDevice(),
            2, // FrameCount
            DXGI_FORMAT_R8G8B8A8_UNORM,
            renderer.GetSrvHeap(),
            renderer.GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
            renderer.GetSrvHeap()->GetGPUDescriptorHandleForHeapStart()
        );
        
        g_ImGuiInitialized = true;
        std::cout << "ImGui initialized successfully (with DX12 backend)" << std::endl;
        
        ShowWindow(hwnd, nCmdShow);
        std::cout << "Window shown, entering main loop..." << std::endl;
        
        // 不自动加载场景，等待用户选择

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                
                // 开始新的 ImGui 帧
                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                
                // 渲染 GUI
                RenderGUI(&renderer);
                
                // 准备 ImGui 渲染
                ImGui::Render();
                
                // 始终渲染 ImGui 到屏幕，保持界面响应
                // 后台离线渲染使用独立的命令资源，不会冲突
                renderer.OnUpdate();
                try {
                    renderer.OnRender();
                } catch (...) {
                    // Ignore all render errors
                }
            }
        }
        
        std::cout << "Exiting main loop, cleaning up..." << std::endl;
        
        // Wait for render thread to complete
        if (g_renderThread && g_renderThread->joinable()) {
            std::cout << "Waiting for render thread to complete..." << std::endl;
            g_renderThread->join();
        }

        renderer.OnDestroy();
        std::cout << "Renderer destroyed" << std::endl;
        
        // 清理 ImGui
        if (g_ImGuiInitialized) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
            std::cout << "ImGui cleaned up" << std::endl;
        }
        
        // 清理日志
        if (g_coutRedirector) {
            delete g_coutRedirector;
            g_coutRedirector = nullptr;
        }
        if (g_cerrRedirector) {
            delete g_cerrRedirector;
            g_cerrRedirector = nullptr;
        }
        if (g_logFile.is_open()) {
            g_logFile.close();
        }
        
        FreeConsole();
        return static_cast<char>(msg.wParam);
    }
    catch (const std::exception& e) {
        std::cerr << "EXCEPTION CAUGHT: " << e.what() << std::endl;
        std::string errorMsg = std::string("Fatal error: ") + e.what();
        AddLogMessage(errorMsg);
        MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR);
        renderer.OnDestroy();
        
        if (g_ImGuiInitialized) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
        }
        
        // 清理日志
        if (g_coutRedirector) {
            delete g_coutRedirector;
            g_coutRedirector = nullptr;
        }
        if (g_cerrRedirector) {
            delete g_cerrRedirector;
            g_cerrRedirector = nullptr;
        }
        if (g_logFile.is_open()) {
            g_logFile.close();
        }
        
        FreeConsole();
        return -1;
    }

    return 0;
}

// GUI渲染函数
void RenderGUI(ACG::Renderer* renderer) {
    static bool isRendering = false;
    static int samplesPerPixel = 100;
    static int maxBounces = 5;
    static char modelPath[512] = "";
    static char outputPath[512] = "";
    static char envMapPath[512] = "";
    static std::string renderStatus = "";
    static bool showRenderStatus = false;
    static std::chrono::steady_clock::time_point renderStartTime;
    static float lastRenderTime = 0.0f;
    
    // Initialize environment light on first call
    static bool envLightInitialized = false;
    if (!envLightInitialized) {
        renderer->SetEnvironmentLightIntensity(0.5f);
        envLightInitialized = true;
    }
    
    // Initialize output path with absolute path on first call
    static bool outputPathInitialized = false;
    if (!outputPathInitialized && !g_exeDirectory.empty()) {
        std::string defaultOutput = g_exeDirectory + "\\output.ppm";
        strncpy_s(outputPath, defaultOutput.c_str(), sizeof(outputPath) - 1);
        outputPathInitialized = true;
    }
    
    // Show render status popup
    if (showRenderStatus) {
        ImGui::OpenPopup("Render Status");
        showRenderStatus = false;
    }
    if (ImGui::BeginPopupModal("Render Status", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", renderStatus.c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    
    // Render Settings 窗口
    ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    ImGui::Text("Output Resolution");
    static int width = 1280;
    static int height = 720;
    ImGui::InputInt("Width", &width);
    ImGui::InputInt("Height", &height);
    
    ImGui::Separator();
    ImGui::Text("Sampling");
    if (ImGui::InputInt("Samples Per Pixel", &samplesPerPixel)) {
        if (samplesPerPixel < 1) samplesPerPixel = 1;
    }
    ImGui::TextDisabled("(Total number of samples to accumulate per pixel)");
    
    if (ImGui::InputInt("Max Bounces", &maxBounces)) {
        if (maxBounces < 1) maxBounces = 1;
        renderer->SetMaxBounces(maxBounces);
    }
    
    ImGui::Separator();
    ImGui::Text("Lighting");
    
    // Environment Light Section
    if (ImGui::TreeNode("Environment Light")) {
        ImGui::TextDisabled("(HDR/EXR skybox for indirect lighting)");
        
        ImGui::InputText("Environment Map", envMapPath, sizeof(envMapPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##EnvMap")) {
            std::string path = OpenFileDialog(g_hwnd, 
                "HDR/EXR Images\0*.hdr;*.exr\0All Files\0*.*\0\0",
                "Select Environment Map");
            if (!path.empty()) {
                strncpy_s(envMapPath, path.c_str(), sizeof(envMapPath) - 1);
            }
        }
        ImGui::TextDisabled("(Will be loaded automatically on Start Render)");
        
        static float envLightIntensity = 0.50f;
        if (ImGui::SliderFloat("Intensity##EnvLight", &envLightIntensity, 0.0f, 10.0f)) {
            renderer->SetEnvironmentLightIntensity(envLightIntensity);
        }
        ImGui::TextDisabled("(Multiplier for environment map brightness)");
        
        ImGui::TreePop();
    }
    
    // Directional Sun Light Section
    if (ImGui::TreeNode("Directional Sun Light")) {
        ImGui::TextDisabled("(Distant directional light source)");
        
        static float sunIntensity = 0.0f;  // Default disabled
        if (ImGui::SliderFloat("Intensity##SunLight", &sunIntensity, 0.0f, 20.0f)) {
            renderer->SetSunIntensity(sunIntensity);
            renderer->ResetAccumulation();
        }
        ImGui::TextDisabled("(Set to 0 to disable sun light)");
        
        static float sunAzimuth = 45.0f;   // degrees, 0..360
        static float sunElevation = 45.0f; // degrees, -90..90
        bool sunDirChanged = false;
        if (ImGui::SliderFloat("Azimuth (deg)", &sunAzimuth, 0.0f, 360.0f)) sunDirChanged = true;
        if (ImGui::SliderFloat("Elevation (deg)", &sunElevation, -89.9f, 89.9f)) sunDirChanged = true;
        if (sunDirChanged) {
            // Convert spherical angles to direction vector (Y-up)
            float az = glm::radians(sunAzimuth);
            float el = glm::radians(sunElevation);
            glm::vec3 dir = glm::vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
            renderer->SetSunDirection(dir);
            renderer->ResetAccumulation();
        }
        
        static float sunColor[3] = {1.0f, 1.0f, 1.0f};
        if (ImGui::ColorEdit3("Color##SunLight", sunColor)) {
            renderer->SetSunColor(glm::vec3(sunColor[0], sunColor[1], sunColor[2]));
            renderer->ResetAccumulation();
        }
        
        ImGui::TreePop();
    }
    
    ImGui::Separator();
    ImGui::Text("Scene");
    ImGui::InputText("Model Path", modelPath, sizeof(modelPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        std::string path = OpenFileDialog(g_hwnd, 
            "3D Models\0*.obj;*.fbx;*.gltf\0All Files\0*.*\0\0",
            "Select 3D Model");
        if (!path.empty()) {
            strncpy_s(modelPath, path.c_str(), sizeof(modelPath) - 1);
        }
    }
    
    ImGui::InputText("Output Path", outputPath, sizeof(outputPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse##Output")) {
        std::string path = SaveFileDialog(g_hwnd,
            "PPM Image\0*.ppm\0All Files\0*.*\0\0",
            "Save Output Image");
        if (!path.empty()) {
            strncpy_s(outputPath, path.c_str(), sizeof(outputPath) - 1);
        }
    }
    
    static bool autoRenderOnLoad = false;
    ImGui::Checkbox("Auto-render on load", &autoRenderOnLoad);
    
    ImGui::End();
    
    // Controls 窗口
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    // Check if render completed
    if (g_renderComplete.load()) {
        g_renderComplete.store(false);
        g_isRendering.store(false);
        std::lock_guard<std::mutex> lock(g_renderMutex);
        renderStatus = g_renderResultMessage;
        showRenderStatus = true;
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
            if (strlen(modelPath) > 0 && strlen(outputPath) > 0) {
            // Copy parameters
            std::string modelPathStr = modelPath;
            std::string outputPathStr = outputPath;
            std::string envMapPathStr = envMapPath;
            int samples = samplesPerPixel;
            int bounces = maxBounces;
            int renderWidth = width;
            int renderHeight = height;
            
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
            bool needsEnvMapLoad = (!envMapPathStr.empty() && g_lastLoadedEnvMap != envMapPathStr);
            
            // Record start time
            renderStartTime = std::chrono::steady_clock::now();
            
            g_renderThread = std::make_unique<std::thread>([renderer, modelPathStr, outputPathStr, envMapPathStr, samples, bounces, renderWidth, renderHeight, needsSceneLoad, needsEnvMapLoad]() {
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
                        renderer->LoadSceneAsync(modelPathStr);
                    } else {
                        std::cout << "[Async] Using already loaded scene" << std::endl;
                        std::cout.flush();
                    }
                    
                    // Load environment map if specified and changed
                    if (needsEnvMapLoad) {
                        std::cout << "[Async] Loading environment map: " << envMapPathStr << std::endl;
                        std::cout.flush();
                        try {
                            renderer->SetEnvironmentMap(envMapPathStr);
                            g_lastLoadedEnvMap = envMapPathStr;
                            std::cout << "[Async] Environment map loaded successfully" << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "[Async] Failed to load environment map: " << e.what() << std::endl;
                        }
                    } else if (!envMapPathStr.empty()) {
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
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - renderStartTime);
                    lastRenderTime = duration.count() / 1000.0f;  // Convert to seconds
                    
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
            if (strlen(modelPath) == 0) renderStatus = "Please select a model file first";
            else renderStatus = "Please specify an output path first";
            showRenderStatus = true;
            std::cerr << renderStatus << std::endl;
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
    
    // Camera Settings 窗口
    ImGui::Begin("Camera Settings");
        
        ACG::Camera* camera = renderer->GetCamera();
        if (camera) {
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
            
            ImGui::Separator();
            ImGui::Text("Camera Target");
            glm::vec3 target = camera->GetTarget();
            if (ImGui::InputFloat("X##Target", &target.x, 0.1f, 1.0f, "%.2f")) {
                camera->SetTarget(target);
            }
            if (ImGui::InputFloat("Y##Target", &target.y, 0.1f, 1.0f, "%.2f")) {
                camera->SetTarget(target);
            }
            if (ImGui::InputFloat("Z##Target", &target.z, 0.1f, 1.0f, "%.2f")) {
                camera->SetTarget(target);
            }
            ImGui::SameLine();
            ImGui::Text("Target");
            
            ImGui::Separator();
            ImGui::Text("Camera Up Vector");
            glm::vec3 up = camera->GetUp();
            if (ImGui::InputFloat("X##Up", &up.x, 0.01f, 0.1f, "%.3f")) {
                camera->SetUp(up);
            }
            if (ImGui::InputFloat("Y##Up", &up.y, 0.01f, 0.1f, "%.3f")) {
                camera->SetUp(up);
            }
            if (ImGui::InputFloat("Z##Up", &up.z, 0.01f, 0.1f, "%.3f")) {
                camera->SetUp(up);
            }
            ImGui::SameLine();
            ImGui::Text("Up");
            
            ImGui::Separator();
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
    
    // Renderer Statistics 窗口
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
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - renderStartTime);
        ImGui::Text("Render Time: %.1f seconds (in progress)", elapsed.count() / 1000.0f);
    } else if (lastRenderTime > 0.0f) {
        ImGui::Text("Render Time: %.1f seconds (last render)", lastRenderTime);
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
    
    // Log Details 窗口
    ImGui::Begin("Log Details");
    
    static bool autoScroll = true;
    if (ImGui::Button("Clear")) {
        g_logMessages.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    
    ImGui::Separator();
    ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    // 显示实际的日志内容
    for (const auto& msg : g_logMessages) {
        ImGui::TextUnformatted(msg.c_str());
    }
    
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
    ImGui::End();
}
