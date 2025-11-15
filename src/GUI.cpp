#include "GUI.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <iostream>

namespace ACG {

GUI::GUI()
    : m_hwnd(nullptr)
    , m_device(nullptr)
    , m_context(nullptr)
{
}

GUI::~GUI() {
    Shutdown();
}

bool GUI::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    m_hwnd = hwnd;
    m_device = device;
    m_context = context;
    
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Set ImGui ini file path to executable directory
    static char iniFilePath[MAX_PATH];
    GetModuleFileNameA(NULL, iniFilePath, MAX_PATH);
    char* lastSlash = strrchr(iniFilePath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
        strcat_s(iniFilePath, "imgui.ini");
        io.IniFilename = iniFilePath;
    }
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer backends
    if (!ImGui_ImplWin32_Init(hwnd)) {
        std::cerr << "Failed to initialize ImGui Win32 backend!" << std::endl;
        return false;
    }
    
    if (!ImGui_ImplDX11_Init(device, context)) {
        std::cerr << "Failed to initialize ImGui DX11 backend!" << std::endl;
        return false;
    }
    
    std::cout << "ImGui initialized successfully" << std::endl;
    return true;
}

void GUI::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void GUI::NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void GUI::Render() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void GUI::RenderUI() {
    RenderSettingsPanel();
    RenderCameraPanel();
    RenderStatsPanel();
    RenderControlPanel();
    RenderLogPanel();
}

void GUI::RenderSettingsPanel() {
    if (!m_showSettingsWindow) return;
    
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Render Settings", &m_showSettingsWindow)) {
        ImGui::SeparatorText("Output Resolution");
        ImGui::InputInt("Width", &m_settings.width);
        ImGui::InputInt("Height", &m_settings.height);
        
        ImGui::SeparatorText("Sampling");
        ImGui::InputInt("Samples Per Pixel", &m_settings.samplesPerPixel);
        ImGui::InputInt("Samples Per Frame", &m_settings.samplesPerFrame);
        ImGui::SliderInt("Max Bounces", &m_settings.maxBounces, 1, 10);
        
        ImGui::SeparatorText("Lighting");
        ImGui::SliderFloat("Environment Light", &m_settings.environmentLightIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::TextWrapped("Tip: Use environment light for scenes without light sources.");
        
        ImGui::SeparatorText("Scene");
        ImGui::Text("Model Path:");
        ImGui::PushItemWidth(-80);
        char modelPathBuffer[512];
        strncpy_s(modelPathBuffer, m_settings.modelPath.c_str(), sizeof(modelPathBuffer) - 1);
        if (ImGui::InputText("##modelpath", modelPathBuffer, sizeof(modelPathBuffer))) {
            m_settings.modelPath = modelPathBuffer;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse##model")) {
            if (m_onBrowseModel) m_onBrowseModel();
        }
        
        ImGui::SeparatorText("Output");
        ImGui::Text("Output Path:");
        ImGui::PushItemWidth(-80);
        char outputPathBuffer[512];
        strncpy_s(outputPathBuffer, m_settings.outputPath.c_str(), sizeof(outputPathBuffer) - 1);
        if (ImGui::InputText("##outputpath", outputPathBuffer, sizeof(outputPathBuffer))) {
            m_settings.outputPath = outputPathBuffer;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse##output")) {
            if (m_onBrowseOutput) m_onBrowseOutput();
        }
        
        ImGui::Spacing();
        ImGui::Checkbox("Auto-render on load", &m_settings.autoRender);
    }
    ImGui::End();
}

void GUI::RenderCameraPanel() {
    if (!m_showCameraWindow) return;
    
    ImGui::SetNextWindowPos(ImVec2(420, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 280), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Camera Settings", &m_showCameraWindow)) {
        ImGui::SeparatorText("Camera Position");
        ImGui::DragFloat3("Position", m_settings.cameraPosition, 0.1f, -10.0f, 10.0f, "%.2f");
        
        ImGui::SeparatorText("Camera Target");
        ImGui::DragFloat3("Target", m_settings.cameraTarget, 0.1f, -10.0f, 10.0f, "%.2f");
        
        ImGui::SeparatorText("Camera Up Vector");
        ImGui::DragFloat3("Up", m_settings.cameraUp, 0.01f, -1.0f, 1.0f, "%.3f");
        
        ImGui::SeparatorText("Field of View");
        ImGui::SliderFloat("FOV", &m_settings.fov, 30.0f, 120.0f, "%.1f deg");
        
        ImGui::Spacing();
        ImGui::Separator();
        
        if (ImGui::Button("Reset to Default", ImVec2(-1, 0))) {
            m_settings.cameraPosition[0] = 0.0f;
            m_settings.cameraPosition[1] = 1.0f;
            m_settings.cameraPosition[2] = 2.7f;
            m_settings.cameraTarget[0] = 0.0f;
            m_settings.cameraTarget[1] = 1.0f;
            m_settings.cameraTarget[2] = 0.0f;
            m_settings.cameraUp[0] = 0.0f;
            m_settings.cameraUp[1] = 1.0f;
            m_settings.cameraUp[2] = 0.0f;
            m_settings.fov = 60.0f;
        }
        
        ImGui::Spacing();
        ImGui::TextWrapped("Tip: Position and target define camera view. Up vector should usually be (0,1,0).");
    }
    ImGui::End();
}

void GUI::RenderStatsPanel() {
    if (!m_showStatsWindow) return;
    
    ImGui::SetNextWindowPos(ImVec2(10, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 190), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Render Statistics", &m_showStatsWindow)) {
        ImGui::Text("Status: %s", m_settings.isRendering ? "Rendering..." : "Idle");
        
        if (m_stats.totalFrames > 0) {
            ImGui::Spacing();
            ImGui::Text("Progress: %d / %d frames", m_stats.currentFrame, m_stats.totalFrames);
            ImGui::ProgressBar(m_stats.progress / 100.0f, ImVec2(-1, 0), 
                             std::to_string((int)m_stats.progress).append("%").c_str());
            
            ImGui::Spacing();
            ImGui::Text("Samples: %d / %d", m_stats.accumulatedSamples, m_settings.samplesPerPixel);
            
            ImGui::Spacing();
            ImGui::Text("Render Time: %.2f seconds", m_stats.renderTime);
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Scene Info:");
        ImGui::Text("  Triangles: %d", m_stats.triangleCount);
        ImGui::Text("  Materials: %d", m_stats.materialCount);
    }
    ImGui::End();
}

void GUI::RenderControlPanel() {
    ImGui::SetNextWindowPos(ImVec2(420, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 120), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Controls")) {
        if (m_settings.isRendering) {
            if (ImGui::Button("Stop Render", ImVec2(-1, 30))) {
                if (m_onStopRender) m_onStopRender();
            }
        } else {
            ImGui::BeginDisabled(m_settings.modelPath.empty());
            if (ImGui::Button("Start Render", ImVec2(-1, 30))) {
                if (m_onStartRender) m_onStartRender();
            }
            ImGui::EndDisabled();
        }
        
        ImGui::Spacing();
        ImGui::Checkbox("Settings", &m_showSettingsWindow);
        ImGui::Checkbox("Camera", &m_showCameraWindow);
        ImGui::Checkbox("Statistics", &m_showStatsWindow);
        ImGui::Checkbox("Log", &m_showLogWindow);
    }
    ImGui::End();
}

void GUI::RenderLogPanel() {
    if (!m_showLogWindow) return;
    
    ImGui::SetNextWindowPos(ImVec2(830, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Log Details", &m_showLogWindow)) {
        // Clear button and auto-scroll checkbox
        if (ImGui::Button("Clear")) {
            ClearLog();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScrollLog);
        
        ImGui::Separator();
        
        // Log content in a scrollable child window
        ImGui::BeginChild("LogScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        
        for (const auto& line : m_logLines) {
            ImGui::TextUnformatted(line.c_str());
        }
        
        // Auto-scroll to bottom
        if (m_autoScrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        
        ImGui::EndChild();
    }
    ImGui::End();
}

void GUI::AddLog(const std::string& text) {
    m_logLines.push_back(text);
    
    // Limit log size to prevent memory issues
    if (m_logLines.size() > 1000) {
        m_logLines.erase(m_logLines.begin());
    }
}

void GUI::ClearLog() {
    m_logLines.clear();
}

bool GUI::WantsCaptureMouse() const {
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

bool GUI::WantsCaptureKeyboard() const {
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

} // namespace ACG
