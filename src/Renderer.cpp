#include "Renderer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ACG {

Renderer::Renderer() 
    : m_device(nullptr)
    , m_context(nullptr)
    , m_swapChain(nullptr)
    , m_pathTracingShader(nullptr)
    , m_accumShader(nullptr)
    , m_renderTarget(nullptr)
    , m_renderTargetUAV(nullptr)
    , m_triangleBuffer(nullptr)
    , m_triangleSRV(nullptr)
    , m_materialBuffer(nullptr)
    , m_materialSRV(nullptr)
    , m_lightBuffer(nullptr)
    , m_lightSRV(nullptr)
    , m_constantBuffer(nullptr)
    , m_numTriangles(0)
    , m_numMaterials(0)
    , m_numLights(0)
    , m_width(0)
    , m_height(0)
    , m_samplesPerPixel(1)
    , m_maxBounces(5)
    , m_rrDepth(3)
    , m_frameCount(0)
{
}

Renderer::~Renderer() {
    Cleanup();
}

bool Renderer::Initialize(int width, int height) {
    m_width = width;
    m_height = height;
    
    // TODO: Initialize DirectX 11
    if (!InitializeD3D()) {
        return false;
    }
    
    // TODO: Create compute shaders
    if (!CreateShaders()) {
        return false;
    }
    
    // TODO: Create render buffers
    if (!CreateBuffers()) {
        return false;
    }
    
    std::cout << "Renderer initialized: " << width << "x" << height << std::endl;
    return true;
}

void Renderer::Render(const Scene& scene, const Camera& camera) {
    if (!m_device || !m_context || !m_pathTracingShader) {
        return;
    }
    
    // Upload scene data on first frame
    if (m_frameCount == 0) {
        if (!UploadSceneData(scene)) {
            std::cerr << "Failed to upload scene data" << std::endl;
            return;
        }
    }
    
    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = m_context->Map(m_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr)) {
        struct RenderParams {
            float camPos[3]; float pad0;
            float camDir[3]; float pad1;
            float camRight[3]; float pad2;
            float camUp[3]; float pad3;
            float camFov; float camAspect; float camAperture; float camFocus;
            uint32_t frameIndex;
            uint32_t samplesPerPixel;
            uint32_t maxBounces;
            uint32_t numTriangles;
            uint32_t numLights;
            uint32_t numMaterials;
            uint32_t resX;
            uint32_t resY;
        };
        
        RenderParams* params = static_cast<RenderParams*>(mappedResource.pData);
        glm::vec3 pos = camera.GetPosition();
        glm::vec3 dir = camera.GetDirection();
        glm::vec3 right = camera.GetRight();
        glm::vec3 up = camera.GetUp();
        
        params->camPos[0] = pos.x; params->camPos[1] = pos.y; params->camPos[2] = pos.z;
        params->camDir[0] = dir.x; params->camDir[1] = dir.y; params->camDir[2] = dir.z;
        params->camRight[0] = right.x; params->camRight[1] = right.y; params->camRight[2] = right.z;
        params->camUp[0] = up.x; params->camUp[1] = up.y; params->camUp[2] = up.z;
        params->camFov = camera.GetFOV();
        params->camAspect = static_cast<float>(m_width) / m_height;
        params->camAperture = camera.GetAperture();
        params->camFocus = camera.GetFocusDistance();
        params->frameIndex = m_frameCount;
        params->samplesPerPixel = m_samplesPerPixel;
        params->maxBounces = m_maxBounces;
        params->numTriangles = m_numTriangles;
        params->numLights = m_numLights;
        params->numMaterials = m_numMaterials;
        params->resX = m_width;
        params->resY = m_height;
        
        m_context->Unmap(m_constantBuffer, 0);
    }
    
    // Bind resources
    m_context->CSSetShader(m_pathTracingShader, nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, &m_constantBuffer);
    
    ID3D11ShaderResourceView* srvs[] = { m_triangleSRV, m_materialSRV, m_lightSRV };
    m_context->CSSetShaderResources(0, 3, srvs);
    
    m_context->CSSetUnorderedAccessViews(0, 1, &m_renderTargetUAV, nullptr);
    
    // Dispatch compute shader
    UINT groupsX = (m_width + 7) / 8;
    UINT groupsY = (m_height + 7) / 8;
    
    glm::vec3 pos = camera.GetPosition();
    glm::vec3 dir = camera.GetDirection();
    std::cout << "Dispatching compute shader: " << groupsX << "x" << groupsY << " groups" << std::endl;
    std::cout << "  Camera: pos=[" << pos.x << "," << pos.y << "," << pos.z 
              << "] dir=[" << dir.x << "," << dir.y << "," << dir.z << "]" << std::endl;
    std::cout << "  Triangles: " << m_numTriangles << ", Materials: " << m_numMaterials << std::endl;
    
    m_context->Dispatch(groupsX, groupsY, 1);
    
    // Flush to ensure GPU work is submitted
    m_context->Flush();
    
    // Check device status
    HRESULT deviceStatus = m_device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus)) {
        std::cerr << "ERROR: Device removed! HRESULT: 0x" << std::hex << deviceStatus << std::dec << std::endl;
    } else {
        std::cout << "Device status: OK" << std::endl;
    }
    
    // Unbind resources
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    m_context->CSSetShaderResources(0, 3, nullSRVs);
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    m_context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    
    m_frameCount++;
    
    if (m_frameCount % 10 == 0) {
        std::cout << "Frame " << m_frameCount << " rendered" << std::endl;
    }
}

void Renderer::ResetAccumulation() {
    m_frameCount = 0;
    
    if (m_context && m_renderTarget) {
        // Clear render target
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        // Would need to create a RTV to clear, or use compute shader
        std::cout << "Accumulation reset" << std::endl;
    }
}

void Renderer::GetRenderResult(unsigned char* pixels) {
    if (!m_device || !m_context || !m_renderTarget || !pixels) {
        return;
    }
    
    // Create staging texture for readback
    D3D11_TEXTURE2D_DESC texDesc;
    m_renderTarget->GetDesc(&texDesc);
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        return;
    }
    
    // Copy from GPU to staging texture
    std::cout << "Copying GPU texture to staging..." << std::endl;
    m_context->CopyResource(stagingTexture, m_renderTarget);
    
    // Ensure copy is complete
    m_context->Flush();
    std::cout << "Copy complete, mapping for readback..." << std::endl;
    
    // Map and read data
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        float* src = static_cast<float*>(mapped.pData);
        
        // Debug: check pixel (0,0) and (1,0) for debug info
        int debugIdx0 = 0;
        int debugIdx1 = 4;
        std::cout << "Debug pixel (0,0): R=" << src[debugIdx0] << " G=" << src[debugIdx0+1] 
                  << " B=" << src[debugIdx0+2] << std::endl;
        std::cout << "Debug pixel (1,0): R=" << src[debugIdx1] << " G=" << src[debugIdx1+1] 
                  << " B=" << src[debugIdx1+2] << std::endl;
        
        // Debug: check if we have any non-zero pixels
        int nonZeroPixels = 0;
        float maxValue = 0.0f;
        
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                int srcIdx = y * (mapped.RowPitch / sizeof(float)) + x * 4;
                int dstIdx = (y * m_width + x) * 4;
                
                float r = src[srcIdx + 0];
                float g = src[srcIdx + 1];
                float b = src[srcIdx + 2];
                
                if (r > 0.001f || g > 0.001f || b > 0.001f) {
                    nonZeroPixels++;
                    maxValue = std::max(maxValue, std::max(r, std::max(g, b)));
                }
                
                // Convert float to byte with gamma correction
                pixels[dstIdx + 0] = static_cast<unsigned char>(std::pow(std::clamp(r, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 1] = static_cast<unsigned char>(std::pow(std::clamp(g, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 2] = static_cast<unsigned char>(std::pow(std::clamp(b, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f);
                pixels[dstIdx + 3] = 255;
            }
        }
        
        std::cout << "GPU Result: " << nonZeroPixels << " non-zero pixels (max value: " << maxValue << ")" << std::endl;
        
        m_context->Unmap(stagingTexture, 0);
    }
    
    stagingTexture->Release();
}

void Renderer::SetSamplesPerPixel(int samples) {
    m_samplesPerPixel = samples;
}

void Renderer::SetMaxBounces(int bounces) {
    m_maxBounces = bounces;
}

void Renderer::SetRussianRouletteDepth(int depth) {
    m_rrDepth = depth;
}

bool Renderer::InitializeD3D() {
    HRESULT hr;
    
    // Create device and context
    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);
    
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,                    // Adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // Software rasterizer module
        createDeviceFlags,
        featureLevels,
        numFeatureLevels,
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevel,
        &m_context
    );
    
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device" << std::endl;
        return false;
    }
    
    std::cout << "D3D11 device created successfully" << std::endl;
    return true;
}

bool Renderer::CreateShaders() {
    HRESULT hr;
    
    // Compile path tracing compute shader
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    shaderFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    
    hr = D3DCompileFromFile(
        L"shaders/PathTracing.hlsl",
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        "cs_5_0",
        shaderFlags,
        0,
        &shaderBlob,
        &errorBlob
    );
    
    if (FAILED(hr)) {
        if (errorBlob) {
            std::cerr << "Shader compilation error: " 
                     << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
            errorBlob->Release();
        }
        std::cerr << "Failed to compile shader" << std::endl;
        return false;
    }
    
    // Check for warnings even if compilation succeeded
    if (errorBlob) {
        std::cout << "Shader compilation warnings: " 
                 << static_cast<char*>(errorBlob->GetBufferPointer()) << std::endl;
        errorBlob->Release();
    }
    
    hr = m_device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &m_pathTracingShader
    );
    
    shaderBlob->Release();
    
    if (FAILED(hr)) {
        std::cerr << "Failed to create compute shader" << std::endl;
        return false;
    }
    
    std::cout << "Shaders compiled successfully" << std::endl;
    return true;
}

bool Renderer::CreateBuffers() {
    HRESULT hr;
    
    // Create render target texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    
    hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_renderTarget);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target texture" << std::endl;
        return false;
    }
    
    // Create UAV for compute shader output
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = texDesc.Format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    
    hr = m_device->CreateUnorderedAccessView(m_renderTarget, &uavDesc, &m_renderTargetUAV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create UAV" << std::endl;
        return false;
    }
    
    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = ((sizeof(float) * 32 + sizeof(uint32_t) * 8 + 255) / 256) * 256; // Align to 256 bytes
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create constant buffer" << std::endl;
        return false;
    }
    
    std::cout << "Render buffers created" << std::endl;
    return true;
}

void Renderer::CleanupSceneBuffers() {
    if (m_triangleSRV) { m_triangleSRV->Release(); m_triangleSRV = nullptr; }
    if (m_triangleBuffer) { m_triangleBuffer->Release(); m_triangleBuffer = nullptr; }
    if (m_materialSRV) { m_materialSRV->Release(); m_materialSRV = nullptr; }
    if (m_materialBuffer) { m_materialBuffer->Release(); m_materialBuffer = nullptr; }
    if (m_lightSRV) { m_lightSRV->Release(); m_lightSRV = nullptr; }
    if (m_lightBuffer) { m_lightBuffer->Release(); m_lightBuffer = nullptr; }
}

bool Renderer::UploadSceneData(const Scene& scene) {
    CleanupSceneBuffers();
    
    HRESULT hr;
    
    // Prepare triangle data
    struct GPUTriangle {
        float v0[3], v1[3], v2[3];
        float n0[3], n1[3], n2[3];
        uint32_t materialIndex;
        uint32_t padding[3];
    };
    
    std::vector<GPUTriangle> triangles;
    for (const auto& mesh : scene.GetMeshes()) {
        const auto& vertices = mesh->GetVertices();
        const auto& indices = mesh->GetIndices();
        uint32_t matIdx = mesh->GetMaterialIndex();
        
        for (size_t i = 0; i < indices.size(); i += 3) {
            GPUTriangle tri;
            const auto& v0 = vertices[indices[i]];
            const auto& v1 = vertices[indices[i + 1]];
            const auto& v2 = vertices[indices[i + 2]];
            
            tri.v0[0] = v0.position.x; tri.v0[1] = v0.position.y; tri.v0[2] = v0.position.z;
            tri.v1[0] = v1.position.x; tri.v1[1] = v1.position.y; tri.v1[2] = v1.position.z;
            tri.v2[0] = v2.position.x; tri.v2[1] = v2.position.y; tri.v2[2] = v2.position.z;
            tri.n0[0] = v0.normal.x; tri.n0[1] = v0.normal.y; tri.n0[2] = v0.normal.z;
            tri.n1[0] = v1.normal.x; tri.n1[1] = v1.normal.y; tri.n1[2] = v1.normal.z;
            tri.n2[0] = v2.normal.x; tri.n2[1] = v2.normal.y; tri.n2[2] = v2.normal.z;
            tri.materialIndex = matIdx;
            
            triangles.push_back(tri);
        }
    }
    
    m_numTriangles = static_cast<uint32_t>(triangles.size());
    std::cout << "Uploading " << m_numTriangles << " triangles..." << std::endl;
    
    // Debug: Print first triangle
    if (m_numTriangles > 0) {
        const auto& tri0 = triangles[0];
        std::cout << "  Triangle[0]: v0=[" << tri0.v0[0] << "," << tri0.v0[1] << "," << tri0.v0[2] << "]"
                  << " v1=[" << tri0.v1[0] << "," << tri0.v1[1] << "," << tri0.v1[2] << "]"
                  << " v2=[" << tri0.v2[0] << "," << tri0.v2[1] << "," << tri0.v2[2] << "]"
                  << " matIdx=" << tri0.materialIndex << std::endl;
    }
    
    if (m_numTriangles > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPUTriangle) * m_numTriangles;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPUTriangle);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = triangles.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_triangleBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create triangle buffer" << std::endl;
            return false;
        }
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numTriangles;
        
        hr = m_device->CreateShaderResourceView(m_triangleBuffer, &srvDesc, &m_triangleSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create triangle SRV" << std::endl;
            return false;
        }
    }
    
    // Prepare material data
    struct GPUMaterial {
        uint32_t type;
        float albedo[3];
        float emission[3];
        float metallic;
        float roughness;
        float ior;
        float transmission;
        uint32_t padding[1];
    };
    
    std::vector<GPUMaterial> materials;
    for (const auto& mat : scene.GetMaterials()) {
        GPUMaterial gmat;
        gmat.type = static_cast<uint32_t>(mat->GetType());
        
        glm::vec3 albedo = mat->GetAlbedo();
        gmat.albedo[0] = albedo.x; gmat.albedo[1] = albedo.y; gmat.albedo[2] = albedo.z;
        
        glm::vec3 emission = mat->GetEmission();
        gmat.emission[0] = emission.x; gmat.emission[1] = emission.y; gmat.emission[2] = emission.z;
        
        gmat.metallic = mat->GetMetallic();
        gmat.roughness = mat->GetRoughness();
        gmat.ior = mat->GetIOR();
        gmat.transmission = mat->GetTransmission();
        
        // Debug output
        if (glm::length(emission) > 0.01f) {
            std::cout << "  GPU Material type=" << gmat.type 
                     << " emission=[" << emission.x << "," << emission.y << "," << emission.z << "]" << std::endl;
        }
        
        materials.push_back(gmat);
    }
    
    m_numMaterials = static_cast<uint32_t>(materials.size());
    std::cout << "Uploading " << m_numMaterials << " materials..." << std::endl;
    
    if (m_numMaterials > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPUMaterial) * m_numMaterials;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPUMaterial);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = materials.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_materialBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create material buffer" << std::endl;
            return false;
        }
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numMaterials;
        
        hr = m_device->CreateShaderResourceView(m_materialBuffer, &srvDesc, &m_materialSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create material SRV" << std::endl;
            return false;
        }
    }
    
    // Prepare light data (placeholder for now)
    struct GPULight {
        uint32_t type;
        float color[3];
        float intensity;
        float position[3];
        float direction[3];
        float size[2];
        uint32_t padding[2];
    };
    
    std::vector<GPULight> lights;
    for (const auto& light : scene.GetLights()) {
        GPULight glight;
        glight.type = static_cast<uint32_t>(light->GetType());
        
        glm::vec3 color = light->GetColor();
        glight.color[0] = color.x; glight.color[1] = color.y; glight.color[2] = color.z;
        glight.intensity = light->GetIntensity();
        
        // Type-specific data would go here
        glight.position[0] = glight.position[1] = glight.position[2] = 0.0f;
        glight.direction[0] = glight.direction[1] = glight.direction[2] = 0.0f;
        glight.size[0] = glight.size[1] = 0.0f;
        
        lights.push_back(glight);
    }
    
    m_numLights = static_cast<uint32_t>(lights.size());
    
    if (m_numLights > 0) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(GPULight) * m_numLights;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(GPULight);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = lights.data();
        
        hr = m_device->CreateBuffer(&bufferDesc, &initData, &m_lightBuffer);
        if (FAILED(hr)) {
            std::cerr << "Failed to create light buffer" << std::endl;
            return false;
        }
        
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_numLights;
        
        hr = m_device->CreateShaderResourceView(m_lightBuffer, &srvDesc, &m_lightSRV);
        if (FAILED(hr)) {
            std::cerr << "Failed to create light SRV" << std::endl;
            return false;
        }
    }
    
    std::cout << "Scene data uploaded successfully" << std::endl;
    return true;
}

void Renderer::Cleanup() {
    CleanupSceneBuffers();
    
    if (m_constantBuffer) {
        m_constantBuffer->Release();
        m_constantBuffer = nullptr;
    }
    if (m_renderTargetUAV) {
        m_renderTargetUAV->Release();
        m_renderTargetUAV = nullptr;
    }
    
    if (m_renderTarget) {
        m_renderTarget->Release();
        m_renderTarget = nullptr;
    }
    
    if (m_accumShader) {
        m_accumShader->Release();
        m_accumShader = nullptr;
    }
    
    if (m_pathTracingShader) {
        m_pathTracingShader->Release();
        m_pathTracingShader = nullptr;
    }
    
    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    
    if (m_context) {
        m_context->Release();
        m_context = nullptr;
    }
    
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    
    std::cout << "Renderer cleaned up" << std::endl;
}

} // namespace ACG
