#include "Scene.h"
#include "SceneLoader.h"
#include "Texture.h"
#include <iostream>
#include <limits>
#include <filesystem>
#include <set>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace ACG {

Scene::Scene() 
    : m_name("Untitled Scene")
    , m_bboxMin(std::numeric_limits<float>::max())
    , m_bboxMax(std::numeric_limits<float>::lowest())
{
}

Scene::~Scene() {
}

void Scene::AddMesh(std::shared_ptr<Mesh> mesh) {
    m_meshes.push_back(mesh);
}

void Scene::AddMaterial(std::shared_ptr<Material> material) {
    m_materials.push_back(material);
}

void Scene::AddLight(std::shared_ptr<Light> light) {
    m_lights.push_back(light);
}

bool Scene::LoadFromFile(const std::string& filename) {
    SceneLoadConfig config;
    return LoadFromFileEx(filename, config);
}

bool Scene::LoadFromFileEx(const std::string& filename, const SceneLoadConfig& config) {
    std::cout << "============================================" << std::endl;
    std::cout << "Scene Loading" << std::endl;
    std::cout << "File: " << filename << std::endl;
    std::cout << "============================================" << std::endl;
    
    // Extract scene name from filename
    std::filesystem::path filePath(filename);
    m_name = filePath.stem().string();
    
    // Only ACG files are supported - no automatic conversion
    if (filePath.extension() != ".acg") {
        std::cerr << "ERROR: Only .acg files are supported for loading." << std::endl;
        std::cerr << "Please convert your scene to ACG format first using the GUI conversion tool." << std::endl;
        std::cerr << "Attempted to load: " << filename << std::endl;
        return false;
    }
    
    std::cout << "Loading binary ACG file: " << filename << std::endl;
    std::string loadPath = filename;
    
    try {
        // Load scene from binary file
        auto loadedScene = SceneLoader::Load(loadPath);
        
        // Transfer data from loaded scene to this scene
        m_meshes = loadedScene->GetMeshes();
        m_materials = loadedScene->GetMaterials();
        m_lights = loadedScene->GetLights();
        
        // Post-processing
        ComputeBoundingBox();
        CollectAllMaterialLayers();
        EstimateMemoryUsage();
        
        // Print statistics
        LoadStats stats = GetLoadStats();
        std::cout << "\n============================================" << std::endl;
        std::cout << "Scene Loaded Successfully!" << std::endl;
        std::cout << "  Meshes: " << stats.totalMeshes << std::endl;
        std::cout << "  Vertices: " << stats.totalVertices << std::endl;
        std::cout << "  Triangles: " << stats.totalTriangles << std::endl;
        std::cout << "  Materials: " << stats.totalMaterials << std::endl;
        std::cout << "  Textures: " << stats.totalTextures << std::endl;
        std::cout << "  Material Layers: " << stats.totalMaterialLayers << std::endl;
        std::cout << "  Memory: " << stats.estimatedMemoryMB << " MB" << std::endl;
        std::cout << "  Bounding Box: [" << m_bboxMin.x << ", " << m_bboxMin.y << ", " << m_bboxMin.z 
                  << "] to [" << m_bboxMax.x << ", " << m_bboxMax.y << ", " << m_bboxMax.z << "]" << std::endl;
        std::cout << "============================================" << std::endl;
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to load scene: " << e.what() << std::endl;
        return false;
    }
}

void Scene::ComputeBoundingBox() {
    m_bboxMin = glm::vec3(std::numeric_limits<float>::max());
    m_bboxMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (auto& mesh : m_meshes) {
        const auto& verts = mesh->GetVertices();
        for (const auto& v : verts) {
            m_bboxMin = glm::min(m_bboxMin, v.position);
            m_bboxMax = glm::max(m_bboxMax, v.position);
        }
    }
}

void Scene::EstimateMemoryUsage() {
    m_loadStats.totalMeshes = static_cast<int>(m_meshes.size());
    m_loadStats.totalMaterials = static_cast<int>(m_materials.size());
    m_loadStats.totalTriangles = 0;
    m_loadStats.totalVertices = 0;
    
    size_t vertexMemory = 0;
    size_t indexMemory = 0;
    
    for (const auto& mesh : m_meshes) {
        int numVerts = static_cast<int>(mesh->GetVertices().size());
        int numIndices = static_cast<int>(mesh->GetIndices().size());
        
        m_loadStats.totalVertices += numVerts;
        m_loadStats.totalTriangles += numIndices / 3;
        
        // Vertex size: position(12) + normal(12) + texCoord(8) + tangent(12) = 44 bytes (padded to 48)
        vertexMemory += numVerts * 48;
        
        // Index buffer: 4 bytes per index
        indexMemory += numIndices * 4;
    }
    
    // Material buffer: 128 bytes per material (estimated)
    size_t materialMemory = m_materials.size() * 128;
    
    // Texture memory estimation
    std::set<const Texture*> uniqueTextures;
    size_t textureMemory = 0;
    
    for (const auto& mat : m_materials) {
        auto baseColorTex = mat->GetBaseColorTexture();
        if (baseColorTex && baseColorTex->GetWidth() > 0) {
            if (uniqueTextures.find(baseColorTex.get()) == uniqueTextures.end()) {
                uniqueTextures.insert(baseColorTex.get());
                // RGBA8: 4 bytes per pixel
                textureMemory += baseColorTex->GetWidth() * baseColorTex->GetHeight() * 4;
            }
        }
    }
    
    m_loadStats.totalTextures = static_cast<int>(uniqueTextures.size());
    
    // Total memory (convert to MB)
    size_t totalBytes = vertexMemory + indexMemory + materialMemory + textureMemory;
    m_loadStats.estimatedMemoryMB = totalBytes / (1024 * 1024);
}

// 收集所有材质的层数据到统一数组
void Scene::CollectAllMaterialLayers() {
    m_materialLayers.clear();
    
    for (size_t i = 0; i < m_materials.size(); ++i) {
        auto& material = m_materials[i];
        
        // 设置材质的层数据起始索引
        material->SetExtendedDataBaseIndex(static_cast<uint32_t>(m_materialLayers.size()));
        
        // 收集所有启用的层
        if (auto layer = material->GetClearcoatLayer()) {
            MaterialExtendedData data;
            data.clearcoat = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetTransmissionLayer()) {
            MaterialExtendedData data;
            data.transmission = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetSheenLayer()) {
            MaterialExtendedData data;
            data.sheen = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetSubsurfaceLayer()) {
            MaterialExtendedData data;
            data.subsurface = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetAnisotropyLayer()) {
            MaterialExtendedData data;
            data.anisotropy = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetIridescenceLayer()) {
            MaterialExtendedData data;
            data.iridescence = *layer;
            m_materialLayers.push_back(data);
        }
        if (auto layer = material->GetVolumeLayer()) {
            MaterialExtendedData data;
            data.volume = *layer;
            m_materialLayers.push_back(data);
        }
    }
    
    m_loadStats.totalMaterialLayers = static_cast<int>(m_materialLayers.size());
    
    std::cout << "[Scene] Collected " << m_materialLayers.size() 
              << " material layers from " << m_materials.size() << " materials" << std::endl;
}

// 添加单个材质层
uint32_t Scene::AddMaterialLayer(const MaterialExtendedData& layer) {
    uint32_t index = static_cast<uint32_t>(m_materialLayers.size());
    m_materialLayers.push_back(layer);
    m_loadStats.totalMaterialLayers = static_cast<int>(m_materialLayers.size());
    return index;
}

} // namespace ACG

