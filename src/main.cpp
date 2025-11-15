#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include <chrono>
#include <Windows.h>
#include <Commdlg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "Renderer.h"
#include "Scene.h"
#include "Camera.h"
#include "GUI.h"
#include "LogRedirector.h"

using namespace ACG;
namespace fs = std::filesystem;

// Forward declare Win32 message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool SavePPM(const std::string& filename, int width, int height, const unsigned char* rgba)
{
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    // Convert RGBA to RGB while writing
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = &rgba[(y * width + x) * 4];
            ofs.put(static_cast<char>(p[0]));
            ofs.put(static_cast<char>(p[1]));
            ofs.put(static_cast<char>(p[2]));
        }
    }
    return true;
}

// Global state for window procedure
static GUI* g_gui = nullptr;
static bool g_running = true;

// Win32 window procedure
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        g_running = false;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    std::cout << "=== ACG Path Tracing Renderer ===" << std::endl;
    std::cout << "Advanced Computer Graphics Project - Fall 2025" << std::endl;
    std::cout << std::endl;

    // Create window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"ACGPathTracer";
    RegisterClassExW(&wc);

    // Create window
    const int windowWidth = 1280;
    const int windowHeight = 720;
    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"ACG Path Tracer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        NULL, NULL,
        hInstance,
        NULL
    );

    if (!hwnd) {
        std::cerr << "Failed to create window!" << std::endl;
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    std::cout << "Window created successfully" << std::endl;

    // Initialize renderer
    std::cout << "Initializing renderer..." << std::endl;
    Renderer renderer;
    if (!renderer.Initialize(windowWidth, windowHeight)) {
        std::cerr << "Failed to initialize renderer!" << std::endl;
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // Get D3D11 device and context from renderer
    std::cout << "Getting D3D11 device and context..." << std::endl;
    ID3D11Device* device = renderer.GetDevice();
    ID3D11DeviceContext* context = renderer.GetContext();

    if (!device || !context) {
        std::cerr << "Failed to get D3D11 device or context!" << std::endl;
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    // Create swap chain for window display
    std::cout << "Creating swap chain for GUI..." << std::endl;
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = windowWidth;
    swapChainDesc.BufferDesc.Height = windowHeight;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* dxgiAdapter = nullptr;
    IDXGIFactory* dxgiFactory = nullptr;

    HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (SUCCEEDED(hr)) {
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (SUCCEEDED(hr)) {
            hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory);
            if (SUCCEEDED(hr)) {
                hr = dxgiFactory->CreateSwapChain(device, &swapChainDesc, &swapChain);
                dxgiFactory->Release();
            }
            dxgiAdapter->Release();
        }
        dxgiDevice->Release();
    }

    if (!swapChain) {
        std::cerr << "Failed to create swap chain!" << std::endl;
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // Create render target view from swap chain back buffer
    ID3D11Texture2D* backBuffer = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (SUCCEEDED(hr)) {
        hr = device->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView);
        backBuffer->Release();
    }

    if (!renderTargetView) {
        std::cerr << "Failed to create render target view!" << std::endl;
        swapChain->Release();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    std::cout << "Swap chain and render target created successfully" << std::endl;

    // Initialize GUI
    std::cout << "Initializing GUI..." << std::endl;
    GUI gui;
    g_gui = &gui;
    if (!gui.Initialize(hwnd, device, context)) {
        std::cerr << "Failed to initialize GUI!" << std::endl;
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "GUI initialized successfully" << std::endl;
    
    // Setup log redirection to GUI
    LogRedirector coutRedirector(std::cout, [&gui](const std::string& text) {
        gui.AddLog(text);
    });
    LogRedirector cerrRedirector(std::cerr, [&gui](const std::string& text) {
        gui.AddLog("[ERROR] " + text);
    });

    // Load default scene if specified (command line parsing for WinMain)
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        // Convert wide string to multi-byte
        char absPath[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, argvW[1], -1, absPath, MAX_PATH, NULL, NULL);
        
        // Convert to absolute path
        char fullPath[MAX_PATH];
        if (GetFullPathNameA(absPath, MAX_PATH, fullPath, NULL)) {
            gui.GetSettings().modelPath = fullPath;
        } else {
            gui.GetSettings().modelPath = absPath;
        }
        gui.GetSettings().autoRender = false;
        std::cout << "Loaded scene: " << gui.GetSettings().modelPath << std::endl;
    }
    if (argvW) LocalFree(argvW);

    // Application state
    bool renderRequested = false;
    Scene scene;
    Camera camera;
    RenderStats stats;
    auto renderStartTime = std::chrono::high_resolution_clock::now();

    // Setup callbacks
    gui.SetOnStartRender([&]() {
        renderRequested = true;
    });

    gui.SetOnStopRender([&]() {
        gui.GetSettings().isRendering = false;
        std::cout << "Rendering stopped by user" << std::endl;
    });

    gui.SetOnBrowseModel([&]() {
        // Open file dialog for model selection
        OPENFILENAMEA ofn = {};
        char szFile[260] = {0};
        
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "3D Models\0*.obj;*.fbx;*.gltf;*.glb;*.dae\0OBJ Files\0*.obj\0FBX Files\0*.fbx\0GLTF Files\0*.gltf;*.glb\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        
        if (GetOpenFileNameA(&ofn)) {
            gui.GetSettings().modelPath = szFile;
            std::cout << "Selected model: " << szFile << std::endl;
        }
    });

    gui.SetOnBrowseOutput([&]() {
        // Save file dialog for output selection
        OPENFILENAMEA ofn = {};
        char szFile[260] = "output.ppm";
        
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "PPM Image\0*.ppm\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.lpstrDefExt = "ppm";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        
        if (GetSaveFileNameA(&ofn)) {
            gui.GetSettings().outputPath = szFile;
            std::cout << "Selected output: " << szFile << std::endl;
        }
    });

    std::cout << "GUI initialized. Ready to render." << std::endl;
    std::cout << "Load a scene using the GUI or pass a model path as command line argument." << std::endl;

    // Main message loop
    MSG msg = {};
    while (g_running)
    {
        // Process Win32 messages
        while (PeekMessageW(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                g_running = false;
        }

        if (!g_running)
            break;

        // Start new ImGui frame
        gui.NewFrame();

        // Render GUI
        gui.RenderUI();

        // Handle render request
        if (renderRequested && !gui.GetSettings().isRendering) {
            renderRequested = false;
            gui.GetSettings().isRendering = true;
            
            const auto& settings = gui.GetSettings();
            
            // Validate settings
            if (settings.modelPath.empty()) {
                std::cerr << "No model path specified!" << std::endl;
                gui.GetSettings().isRendering = false;
                continue;
            }

            std::cout << "\n=== Starting Render ===" << std::endl;
            std::cout << "Model: " << settings.modelPath << std::endl;
            std::cout << "Resolution: " << settings.width << "x" << settings.height << std::endl;
            std::cout << "Samples: " << settings.samplesPerPixel << " (" << settings.samplesPerFrame << " per frame)" << std::endl;

            // Reinitialize renderer if resolution changed
            if (settings.width != windowWidth || settings.height != windowHeight) {
                std::cout << "Warning: Resolution change requires application restart" << std::endl;
                std::cout << "Using initial resolution: " << windowWidth << "x" << windowHeight << std::endl;
            }

            // Adjust path if relative (add ../ to go from bin/ to project root)
            std::string modelPath = settings.modelPath;
            if (modelPath.find(":\\") == std::string::npos && modelPath.find(":/") == std::string::npos) {
                // Relative path - adjust for bin directory
                modelPath = "..\\" + modelPath;
            }

            // Load scene
            scene = Scene();
            if (!scene.LoadFromFile(modelPath)) {
                std::cerr << "Failed to load scene: " << settings.modelPath << std::endl;
                gui.GetSettings().isRendering = false;
                continue;
            }

            scene.ComputeBoundingBox();
            
            // Count triangles
            stats.triangleCount = 0;
            for (const auto& mesh : scene.GetMeshes()) {
                stats.triangleCount += static_cast<int>(mesh->GetIndices().size() / 3);
            }
            stats.materialCount = static_cast<int>(scene.GetMaterials().size());

            // Setup camera from GUI settings
            glm::vec3 cameraPos = glm::vec3(
                settings.cameraPosition[0], 
                settings.cameraPosition[1], 
                settings.cameraPosition[2]
            );
            glm::vec3 cameraTarget = glm::vec3(
                settings.cameraTarget[0], 
                settings.cameraTarget[1], 
                settings.cameraTarget[2]
            );
            glm::vec3 cameraUp = glm::vec3(
                settings.cameraUp[0], 
                settings.cameraUp[1], 
                settings.cameraUp[2]
            );
            
            camera.SetPosition(cameraPos);
            camera.SetTarget(cameraTarget);
            camera.SetUp(cameraUp);
            camera.SetFOV(settings.fov);
            camera.SetAspectRatio(static_cast<float>(settings.width) / settings.height);
            camera.SetAperture(0.0f);
            camera.SetFocusDistance(glm::length(cameraPos - cameraTarget));

            renderer.SetSamplesPerPixel(settings.samplesPerPixel);
            renderer.SetMaxBounces(settings.maxBounces);
            renderer.SetRussianRouletteDepth(3);
            renderer.SetEnvironmentLight(settings.environmentLightIntensity);
            renderer.ResetAccumulation();

            // Calculate frames
            stats.totalFrames = (settings.samplesPerPixel + settings.samplesPerFrame - 1) / settings.samplesPerFrame;
            stats.currentFrame = 0;
            stats.accumulatedSamples = 0;
            stats.progress = 0.0f;
            
            renderStartTime = std::chrono::high_resolution_clock::now();
        }

        // Process rendering frame by frame
        if (gui.GetSettings().isRendering) {
            const auto& settings = gui.GetSettings();
            
            if (stats.currentFrame < stats.totalFrames) {
                int samplesToRender = std::min(
                    settings.samplesPerFrame, 
                    settings.samplesPerPixel - renderer.GetAccumulatedSamples()
                );
                
                renderer.RenderFrame(scene, camera, samplesToRender);
                
                stats.currentFrame++;
                stats.accumulatedSamples = renderer.GetAccumulatedSamples();
                stats.progress = 100.0f * stats.currentFrame / stats.totalFrames;
                
                auto now = std::chrono::high_resolution_clock::now();
                stats.renderTime = std::chrono::duration<float>(now - renderStartTime).count();
                
                gui.UpdateStats(stats);
                
                // Log progress
                if (stats.currentFrame % 10 == 0 || stats.currentFrame == stats.totalFrames) {
                    std::cout << "Progress: " << stats.currentFrame << "/" << stats.totalFrames 
                             << " frames (" << stats.accumulatedSamples << "/" << settings.samplesPerPixel 
                             << " samples, " << std::fixed << std::setprecision(1) << stats.progress << "%)" << std::endl;
                }
            } else {
                // Rendering complete
                std::cout << "\nRendering complete! Saving image..." << std::endl;
                
                std::vector<unsigned char> pixels(settings.width * settings.height * 4);
                renderer.GetRenderResult(pixels.data());
                
                if (SavePPM(settings.outputPath, settings.width, settings.height, pixels.data())) {
                    std::cout << "Saved image: " << settings.outputPath << std::endl;
                } else {
                    std::cerr << "Failed to save image: " << settings.outputPath << std::endl;
                }
                
                gui.GetSettings().isRendering = false;
                std::cout << "Render complete! Time: " << stats.renderTime << " seconds" << std::endl;
            }
        }

        // Render ImGui
        float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        context->ClearRenderTargetView(renderTargetView, clearColor);
        context->OMSetRenderTargets(1, &renderTargetView, nullptr);
        
        gui.Render();
        
        // Present frame
        swapChain->Present(1, 0);

        // Small sleep to prevent busy waiting
        Sleep(1);
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    gui.Shutdown();
    
    if (renderTargetView)
        renderTargetView->Release();
    if (swapChain)
        swapChain->Release();
    
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}
