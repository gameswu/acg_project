#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <d3d12.h>

// Forward declarations
namespace ACG {
    class Renderer;
}

namespace GUI {

// GUI state structure
struct GUIState {
    // Render settings
    int width = 1280;
    int height = 720;
    int samplesPerPixel = 100;
    int maxBounces = 5;
    char modelPath[512] = "";
    char outputPath[512] = "";
    char envMapPath[512] = "";
    bool autoRenderOnLoad = false;
    bool useCustomMTLParser = true;  // Use custom MTL parser for OBJ files
    
    // Batch loading settings for large scenes
    bool enableBatchLoading = true;
    int maxMeshesPerBatch = 500;
    int maxTexturesPerBatch = 64;
    int maxMemoryMB = 4096;  // 4GB default
    
    // Lighting settings
    float envLightIntensity = 0.5f;
    float sunIntensity = 0.0f;
    float sunAzimuth = 45.0f;
    float sunElevation = 45.0f;
    float sunColor[3] = {1.0f, 1.0f, 1.0f};
    // Camera orbit controls (azimuth/elevation/distance)
    float cameraAzimuth = 45.0f;    // degrees
    float cameraElevation = 0.0f;   // degrees
    float cameraDistance = 3.0f;    // distance from target
    bool cameraAnglesInitialized = false;
    float cameraUpAngle = 0.0f; // roll angle in degrees (rotation around camera forward)
    
    // Internal state
    std::string renderStatus = "";
    bool showRenderStatus = false;
    bool envLightInitialized = false;
    bool outputPathInitialized = false;
    float lastRenderTime = 0.0f;
    float modelLoadTime = 0.0f;
    
    // Render result texture for display
    ID3D12Resource* renderResultTexture = nullptr;
    ID3D12DescriptorHeap* renderResultSRVHeap = nullptr;
    
    // Pointers to external data
    std::vector<std::string>* pLogMessages = nullptr;
};

// Initialize GUI state with default values
void InitializeGUIState(GUIState& state, const std::string& exeDirectory);

// Clean up GUI resources (e.g. render threads)
void ShutdownGUI();

// Render all GUI windows
void RenderGUI(ACG::Renderer* renderer, GUIState& state, HWND hwnd);

// File dialog helpers
std::string OpenFileDialog(HWND hwnd, const char* filter, const char* title);
std::string SaveFileDialog(HWND hwnd, const char* filter, const char* title);

// Individual window rendering functions
void RenderSettingsWindow(ACG::Renderer* renderer, GUIState& state, HWND hwnd);
void RenderControlsWindow(ACG::Renderer* renderer, GUIState& state);
void RenderCameraWindow(ACG::Renderer* renderer, GUIState& state);
void RenderResultWindow(ACG::Renderer* renderer, GUIState& state);
void RenderLogWindow(const std::vector<std::string>& logMessages);

} // namespace GUI
