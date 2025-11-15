#pragma once

#include <string>
#include <functional>
#include <vector>
#include <sstream>
#include <Windows.h>
#include <d3d11.h>

namespace ACG {

struct RenderSettings {
    int width = 1280;
    int height = 720;
    int samplesPerPixel = 1000;
    int samplesPerFrame = 10;
    int maxBounces = 5;
    float fov = 60.0f;
    
    // Camera settings
    float cameraPosition[3] = {0.0f, 1.0f, 2.7f};
    float cameraTarget[3] = {0.0f, 1.0f, 0.0f};
    float cameraUp[3] = {0.0f, 1.0f, 0.0f};
    
    std::string modelPath = "";
    std::string outputPath = "output.ppm";
    
    bool autoRender = false;
    bool isRendering = false;
};

struct RenderStats {
    int currentFrame = 0;
    int totalFrames = 0;
    int accumulatedSamples = 0;
    float progress = 0.0f;
    float renderTime = 0.0f;
    int triangleCount = 0;
    int materialCount = 0;
};

class GUI {
public:
    GUI();
    ~GUI();
    
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    
    void NewFrame();
    void Render();
    
    void RenderUI();
    
    // Getters
    const RenderSettings& GetSettings() const { return m_settings; }
    RenderSettings& GetSettings() { return m_settings; }
    
    void UpdateStats(const RenderStats& stats) { m_stats = stats; }
    
    // Log functions
    void AddLog(const std::string& text);
    void ClearLog();
    
    // Callbacks
    void SetOnStartRender(std::function<void()> callback) { m_onStartRender = callback; }
    void SetOnStopRender(std::function<void()> callback) { m_onStopRender = callback; }
    void SetOnBrowseModel(std::function<void()> callback) { m_onBrowseModel = callback; }
    void SetOnBrowseOutput(std::function<void()> callback) { m_onBrowseOutput = callback; }
    
    bool WantsCaptureMouse() const;
    bool WantsCaptureKeyboard() const;

private:
    void RenderSettingsPanel();
    void RenderCameraPanel();
    void RenderStatsPanel();
    void RenderControlPanel();
    void RenderLogPanel();
    
    HWND m_hwnd;
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    
    RenderSettings m_settings;
    RenderStats m_stats;
    
    // Callbacks
    std::function<void()> m_onStartRender;
    std::function<void()> m_onStopRender;
    std::function<void()> m_onBrowseModel;
    std::function<void()> m_onBrowseOutput;
    
    bool m_showSettingsWindow = true;
    bool m_showCameraWindow = true;
    bool m_showStatsWindow = true;
    bool m_showLogWindow = true;
    
    // Log data
    std::vector<std::string> m_logLines;
    bool m_autoScrollLog = true;
};

} // namespace ACG
