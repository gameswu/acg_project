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
#include "GUI.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <glm/glm.hpp>
#include "resource.h"

#include "GUI.h"

// Global variable for executable directory
std::string g_exeDirectory;
static HWND g_hwnd = nullptr;

// GUI state
static GUI::GUIState g_guiState;

// Forward declare the renderer pointer
ACG::Renderer* g_pRenderer = nullptr;
bool g_ImGuiInitialized = false;

// Global log message storage
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

    // Load application icon from embedded resource
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = hIcon; // Set the class icon

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

    // Set window icons (large and small) for title bar and taskbar
    if (hIcon != NULL) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    try {
        // Open log file
        g_logFile.open("renderer.log", std::ios::out | std::ios::trunc);
        if (g_logFile.is_open()) {
            AddLogMessage("Log file opened successfully");
        }
        
        // Set up log redirection (redirect std::cout and std::cerr to log)
        g_coutRedirector = new ACG::LogRedirector(std::cout, AddLogMessage);
        g_cerrRedirector = new ACG::LogRedirector(std::cerr, AddLogMessage);
        
        std::cout << "Initializing renderer..." << std::endl;
        renderer.OnInit(hwnd);
        std::cout << "Renderer initialized successfully" << std::endl;
        
        // 鍒濆鍖?ImGui
        std::cout << "Initializing ImGui..." << std::endl;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        // Set ImGui ini file path to executable directory
        static std::string iniPath = g_exeDirectory + "\\imgui.ini";
        io.IniFilename = iniPath.c_str();
        
        ImGui::StyleColorsDark();
        
        // Initialize ImGui Win32 backend
        ImGui_ImplWin32_Init(hwnd);
        
        // Initialize ImGui DX12 backend
        // Obtain DX12 resources from Renderer
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
        
        // Initialize GUI state
        GUI::InitializeGUIState(g_guiState, g_exeDirectory);
        g_guiState.pLogMessages = &g_logMessages;
        
        ShowWindow(hwnd, nCmdShow);
        std::cout << "Window shown, entering main loop..." << std::endl;
        
        // Main message loop, handle user input and rendering

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                
                // Start a new ImGui frame
                ImGui_ImplDX12_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                
                // Render GUI
                GUI::RenderGUI(&renderer, g_guiState, g_hwnd);
                
                // Render ImGui
                ImGui::Render();
                
                // Present the rendered ImGui frame and handle window messages
                // Use double buffering to avoid flickering
                renderer.OnUpdate();
                try {
                    renderer.OnRender();
                } catch (...) {
                    // Ignore all render errors
                }
            }
        }
        
        std::cout << "Exiting main loop, cleaning up..." << std::endl;
        
        // Shutdown GUI (this will wait for render thread if running)
        GUI::ShutdownGUI();
        std::cout << "GUI shutdown complete" << std::endl;

        renderer.OnDestroy();
        std::cout << "Renderer destroyed" << std::endl;
        
        // Clean up ImGui
        if (g_ImGuiInitialized) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_ImGuiInitialized = false;
            std::cout << "ImGui cleaned up" << std::endl;
        }
        
        // Clean up log redirection
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
        
        // Clean up log redirection
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
