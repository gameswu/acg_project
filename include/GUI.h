#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <d3d12.h>

// Forward declarations
namespace ACG {
    class Renderer;
    struct PersistentConfig;
}

namespace GUI {

// GUI state structure
struct GUIState {
    // Render settings
    int width = 1280;
    int height = 720;
    int samplesPerPixel = 100;
    int maxBounces = 5;
    char modelPath[512] = "";           // ACG file path for rendering
    char sourceScenePath[512] = "";     // Source scene path for conversion
    char outputAcgPath[512] = "";       // Output ACG path for conversion
    char outputPath[512] = "";
    char envMapPath[512] = "";
    bool autoRenderOnLoad = false;
    bool useCustomMTLParser = true;  // Use custom MTL parser for OBJ files
    
    // Batch loading settings for large scenes
    bool enableBatchLoading = true;
    int maxMeshesPerBatch = 500;
    int maxTexturesPerBatch = 64;
    int maxMemoryMB = 4096;  // 4GB default
    
    // Virtual Texture settings
    int vtTileBatchSize = 50;  // Tiles per GPU batch (affects memory usage)
    
    // Rendering performance settings
    int renderBatchSize = 5;  // Samples per GPU execution (affects TDR timeout, range 1-20)
    
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
    
    // Depth of Field settings
    bool enableDepthOfField = false;
    float focusDistance = 5.0f;
    float aperture = 0.1f;
    
    // Cartoon style settings
    bool simplifiedRendering = false;
    bool specialStyle = false;
    
    // Internal state
    std::string renderStatus = "";
    bool showRenderStatus = false;
    std::string conversionStatus = "";  // Status message for scene conversion
    bool showConversionStatus = false;  // Whether to show conversion status
    bool envLightInitialized = false;
    bool outputPathInitialized = false;
    float lastRenderTime = 0.0f;
    float modelLoadTime = 0.0f;
    
    // Render result texture for display
    ID3D12Resource* renderResultTexture = nullptr;
    ID3D12DescriptorHeap* renderResultSRVHeap = nullptr;
    
    // Pointers to external data
    std::vector<std::string>* pLogMessages = nullptr;
    
    // Callback for adding log messages (writes to both GUI and file)
    std::function<void(const std::string&)> logCallback = nullptr;
};

// Initialize GUI state with default values
void InitializeGUIState(GUIState& state, const std::string& exeDirectory);

// Load persistent configuration
void LoadConfig(GUIState& state, ACG::Renderer* renderer, const std::string& configPath);

// Save persistent configuration  
void SaveConfig(const GUIState& state, ACG::Renderer* renderer, const std::string& configPath);

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
