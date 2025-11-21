#pragma once

#include "DX12Helper.h"
#include "Scene.h"
#include "Camera.h"
#include "Denoiser.h"
#include "VirtualTextureSystem.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <vector>
#include <memory>
#include <atomic>
#include <glm/glm.hpp>

// Forward declare DXC interfaces
struct IDxcBlob;

// Simplified GPU Material structure - only vec4 fields for guaranteed alignment
// Total 96 bytes (6 * 16)
struct alignas(16) GPUMaterial {
    glm::vec4 albedo;        // 0-15: Kd (diffuse color)
    glm::vec4 emission;      // 16-31: Ke (emissive color)
    glm::vec4 specular;      // 32-47: Ks (specular color)
    glm::vec4 params1;       // 48-63: type, metallic, roughness, ior
    glm::vec4 params2;       // 64-79: transmission, albedoTextureIndex, illum, unused
    glm::vec4 params3;       // 80-95: albedoTextureSize.xy, padding.xy
};

namespace ACG {
    class Renderer {
    public:
        Renderer(UINT width, UINT height);
        ~Renderer();

        void OnInit(HWND hwnd);
        void OnUpdate();
        void OnRender();
        void OnDestroy();
        void OnResize(UINT width, UINT height);

        void LoadScene(const std::string& path);
        void LoadSceneAsync(const std::string& path); // Async version using independent command resources
        void RenderToFile(const std::string& outputPath, int samplesPerPixel, int maxBounces);
        void SetEnvironmentMap(const std::string& path);  // Load HDR/EXR environment map
        void ClearEnvironmentMap();  // Clear/unload environment map
        
        // GUI控制方法
        void SetSamplesPerPixel(int spp) { m_samplesPerPixel = spp; }
        void SetMaxBounces(int bounces) { m_maxBounces = bounces; }
        void SetEnvironmentLightIntensity(float intensity) { m_environmentLightIntensity = intensity; }
        void SetSunDirection(const glm::vec3& dir);
        void SetSunColor(const glm::vec3& color);
        void SetSunIntensity(float intensity);
        void ResetAccumulation() { m_accumulatedSamples = 0; }
        int GetAccumulatedSamples() const { return m_accumulatedSamples; }
        int GetSamplesPerPixel() const { return m_samplesPerPixel; }
        int GetMaxBounces() const { return m_maxBounces; }
        Scene* GetScene() { return m_scene.get(); }
        Camera* GetCamera() { return &m_camera; }
        void StopRender() { m_stopRenderRequested = true; }
        
        // ImGui 支持
        ID3D12Device* GetDevice() { return m_device.Get(); }
        ID3D12DescriptorHeap* GetSrvHeap() { return m_imguiSrvHeap.Get(); }

    private:
        struct CameraConstants {
            glm::mat4 viewInverse;
            glm::mat4 projInverse;
            uint32_t frameIndex;
            uint32_t maxBounces;
            float environmentLightIntensity;
            float padding;
            // cameraParams: x = FOV (degrees), y = aspectRatio, z = aperture, w = focusDistance
            glm::vec4 cameraParams;
            // Sun parameters packed as vec4 for safe alignment
            glm::vec4 sunDirIntensity; // xyz = direction (toward light), w = intensity
            glm::vec4 sunColorEnabled; // rgb = color, a = enabled (0 or 1)
        };

        void InitPipeline(HWND hwnd);
        void CreateDevice();
        void CreateCommandQueueAndList();
        void CreateSwapChain(HWND hwnd);
        void CreateDescriptorHeaps();
        void CreateRenderTargets();
        void CreateRaytracingPipeline();
        void CreateAccelerationStructures(ID3D12GraphicsCommandList4* cmdList);
        void CreateShaderResources(ID3D12GraphicsCommandList4* cmdList);
        void CreateShaderBindingTable();
        // Resource creation (allocation only, no data upload)
        void CreateTextureArrayResource(int totalTextures, UINT maxWidth, UINT maxHeight);
        
        // Data upload (assumes resource already created)
        void UploadTextureBatchData(ID3D12GraphicsCommandList4* cmdList, 
                                    const std::vector<std::shared_ptr<Texture>>& textures, 
                                    int startIndex, UINT maxWidth, UINT maxHeight,
                                    std::vector<glm::vec2>* outUvScales = nullptr);
        
        // State transition and descriptor creation
        void FinalizeTextureArray(ID3D12GraphicsCommandList4* cmdList, int totalTextures);
        
        // Virtual Texture System SRV creation
        void CreateVirtualTextureSRVs();
        
        Microsoft::WRL::ComPtr<ID3D12Resource> UploadEnvironmentMap(ID3D12GraphicsCommandList4* cmdList, const std::shared_ptr<Texture>& envMap);
        
        void CheckRaytracingSupport();
        Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filename);
        void CreateRaytracingRootSignature();

        void WaitForGpu();
        void MoveToNextFrame();
        void PopulateCommandList();

        UINT m_width;
        UINT m_height;
        HWND m_hwnd;

        static const UINT FrameCount = 2;
        UINT m_frameIndex;
        HANDLE m_fenceEvent;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        UINT64 m_fenceValue;

        Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
        Microsoft::WRL::ComPtr<ID3D12Device5> m_device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount]; // One per frame
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_commandList;

        Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        UINT m_rtvDescriptorSize;
        
        // Separate descriptor heap for ImGui (to avoid conflicts with offline rendering)
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;

        // DXR
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_raytracingEmptyLocalRootSignature;
        Microsoft::WRL::ComPtr<ID3D12StateObject> m_dxrStateObject;

        // DXR Acceleration Structure
        Microsoft::WRL::ComPtr<ID3D12Resource> m_bottomLevelAS;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_topLevelAS;
        
        // Keep these temporary resources alive until GPU finishes using them
        Microsoft::WRL::ComPtr<ID3D12Resource> m_blasScratchBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_tlasScratchBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceDescBuffer;
        
        // Upload buffers - must be kept alive until GPU copy completes!
        Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_indexUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleMaterialUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_materialUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_materialLayersUpload;  // Upload heap for material layers (新增)
        Microsoft::WRL::ComPtr<ID3D12Resource> m_textureUpload;  // Upload heap for textures

        // DXR Shader Resources
        Microsoft::WRL::ComPtr<ID3D12Resource> m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
        // Geometry buffers
        Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_materialBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_materialLayersBuffer;  // Extended material layers (新增)
        Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleMaterialBuffer; // Material index per triangle
        Microsoft::WRL::ComPtr<ID3D12Resource> m_textureAtlas;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_textureScalesBuffer;  // UV scale factors (float2 per texture)
        Microsoft::WRL::ComPtr<ID3D12Resource> m_textureScalesUpload;  // Upload heap for texture scales
        Microsoft::WRL::ComPtr<ID3D12Resource> m_environmentMap;  // HDR environment map

        // Descriptor indices in the shader-visible heap
        UINT m_srvUavDescriptorSize;
        UINT m_srvIndex_Vertices; // SRV index for vertex buffer
        UINT m_srvIndex_Indices;  // SRV index for index buffer
        UINT m_srvIndex_Materials; // SRV index for materials
        UINT m_srvIndex_MaterialLayers; // SRV index for material layers (新增)
        UINT m_uavIndex_Output;    // UAV index for output texture

        // DXR Shader Binding Table
        Microsoft::WRL::ComPtr<ID3D12Resource> m_sbtBuffer;
        UINT m_sbtEntrySize;
        UINT m_sbtRayGenOffset;
        UINT m_sbtMissOffset;
        UINT m_sbtHitGroupOffset;
        
        // DXR Shader Library
        Microsoft::WRL::ComPtr<IDxcBlob> m_raytracingShaderLibrary;

        std::unique_ptr<Scene> m_scene;
        Camera m_camera;
        
        bool m_dxrSupported;
        
        // Virtual Texture System
        VirtualTextureSystem m_virtualTextureSystem;
        bool m_useVirtualTextures;
        
        // Offline rendering resources (independent from real-time rendering)
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_offlineCommandAllocator;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_offlineFence;
        UINT64 m_offlineFenceValue;
        HANDLE m_offlineFenceEvent;
        std::atomic<bool> m_stopRenderRequested;
        
        // 渲染参数
        int m_samplesPerPixel = 1;
        int m_maxBounces = 5;
        int m_accumulatedSamples = 0;
        float m_environmentLightIntensity = 0.5f;
        // Sun parameters (CPU-side, controlled via intensity)
        glm::vec3 m_sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 m_sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
        float m_sunIntensity = 0.0f;  // 0 = disabled
        std::atomic<bool> m_isRendering;
        
        // OIDN降噪器
        std::unique_ptr<Denoiser> m_denoiser;
    };
}
