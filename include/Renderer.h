#pragma once

#include "DX12Helper.h"
#include "Scene.h"
#include "Camera.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <vector>
#include <memory>
#include <atomic>
#include <glm/glm.hpp>

// Forward declare DXC interfaces
struct IDxcBlob;

// Ensure 16-byte alignment for HLSL compatibility
struct alignas(16) GPUMaterial {
    glm::vec4 albedo;        // 16 bytes - Kd (diffuse color)
    glm::vec4 emission;      // 16 bytes - Ke (emissive color)
    glm::vec4 specular;      // 16 bytes - Ks (specular color)
    uint32_t type;           // 4 bytes - material type enum
    float metallic;          // 4 bytes - Metallic factor (PBR)
    float roughness;         // 4 bytes - Roughness factor (PBR)
    float ior;               // 4 bytes - index of refraction
    float transmission;      // 4 bytes - Transmission factor
    int albedoTextureIndex;  // 4 bytes - Texture index
    int illum;               // 4 bytes - MTL illumination model
    glm::vec2 albedoTextureSize; // 8 bytes
    float padding[3];        // 12 bytes padding
    // Total: 48 + 28 + 8 + 12 = 96 bytes
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
        
        // GUI控制方法
        void SetSamplesPerPixel(int spp) { m_samplesPerPixel = spp; }
        void SetMaxBounces(int bounces) { m_maxBounces = bounces; }
        void SetEnvironmentLightIntensity(float intensity) { m_environmentLightIntensity = intensity; }
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

        // DXR Shader Resources
        Microsoft::WRL::ComPtr<ID3D12Resource> m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;
        // Geometry buffers
        Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_materialBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_triangleMaterialBuffer; // Material index per triangle
        Microsoft::WRL::ComPtr<ID3D12Resource> m_textureAtlas;

        // Descriptor indices in the shader-visible heap
        UINT m_srvUavDescriptorSize;
        UINT m_srvIndex_Vertices; // SRV index for vertex buffer
        UINT m_srvIndex_Indices;  // SRV index for index buffer
        UINT m_srvIndex_Materials; // SRV index for materials
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
        std::atomic<bool> m_isRendering;
    };
}
