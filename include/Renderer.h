#pragma once

#include <memory>
#include <d3d11.h>
#include <dxgi.h>
#include "Scene.h"
#include "Camera.h"

namespace ACG {

/**
 * @brief GPU-based path tracing renderer
 * 
 * This class manages the DirectX 11 rendering pipeline and implements
 * the path tracing algorithm on the GPU using compute shaders.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    // 初始化渲染器
    bool Initialize(int width, int height);
    
    // 渲染一帧
    void Render(const Scene& scene, const Camera& camera);
    
    // 重置累积缓冲区（用于交互式渲染）
    void ResetAccumulation();
    
    // 获取渲染结果
    void GetRenderResult(unsigned char* pixels);
    
    // 设置渲染参数
    void SetSamplesPerPixel(int samples);
    void SetMaxBounces(int bounces);
    void SetRussianRouletteDepth(int depth);

private:
    // DirectX 资源
    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    IDXGISwapChain* m_swapChain;
    
    // 计算着色器
    ID3D11ComputeShader* m_pathTracingShader;
    ID3D11ComputeShader* m_accumShader;
    
    // 渲染目标和缓冲区
    ID3D11Texture2D* m_renderTarget;
    ID3D11UnorderedAccessView* m_renderTargetUAV;
    
    // 渲染参数
    int m_width;
    int m_height;
    int m_samplesPerPixel;
    int m_maxBounces;
    int m_rrDepth;  // Russian Roulette depth
    int m_frameCount;
    
    bool InitializeD3D();
    bool CreateShaders();
    bool CreateBuffers();
    void Cleanup();
};

} // namespace ACG
