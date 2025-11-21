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
    
    std::string loadPath = filename;
    
    // 如果不是ACG文件，需要先转换为ACG
    if (filePath.extension() != ".acg") {
        std::cout << "Converting model file to binary format..." << std::endl;
        std::cout << "Input format: " << filePath.extension().string() << std::endl;
        
        // 获取可执行文件目录
        std::filesystem::path exePath;
#ifdef _WIN32
        wchar_t exePathBuffer[MAX_PATH];
        GetModuleFileNameW(NULL, exePathBuffer, MAX_PATH);
        exePath = std::filesystem::path(exePathBuffer).parent_path();
#else
        exePath = std::filesystem::current_path();
#endif
        
        // 生成临时ACG文件路径（使用bin/tmp目录）
        std::filesystem::path tempDir = exePath / "tmp";
        std::filesystem::create_directories(tempDir);
        std::filesystem::path tempPath = tempDir / (filePath.stem().string() + ".acg");
        
        // 构建Python命令（使用虚拟环境中的Python）
        std::filesystem::path loaderScript = exePath / "loader" / "main.py";
        std::filesystem::path absoluteObjPath = std::filesystem::absolute(filePath);
        
        // 检查loader脚本是否存在
        if (!std::filesystem::exists(loaderScript)) {
            std::cerr << "ERROR: Loader script not found: " << loaderScript << std::endl;
            std::cerr << "Please ensure loader directory exists in bin/" << std::endl;
            return false;
        }
        
        // 使用虚拟环境中的Python
        std::filesystem::path venvPython = exePath / "loader" / ".venv" / "Scripts" / "python.exe";
        std::string pythonExe;
        
        if (std::filesystem::exists(venvPython)) {
            pythonExe = "\"" + venvPython.string() + "\"";
            std::cout << "Using virtual environment Python: " << venvPython << std::endl;
        } else {
            pythonExe = "python";
            std::cout << "WARNING: Virtual environment not found, using system Python" << std::endl;
            std::cout << "Expected path: " << venvPython << std::endl;
        }
        
        std::string pythonCmd = pythonExe + " \"" + loaderScript.string() + "\" \"" + 
                               absoluteObjPath.string() + "\" \"" + 
                               tempPath.string() + "\" --binary";
        
        std::cout << "Running converter..." << std::endl;
        std::cout << "Command: " << pythonCmd << std::endl;
        std::cout << "Output file: " << tempPath << std::endl;
        
#ifdef _WIN32
        // Windows: 重定向输出以捕获错误信息
        std::string cmdWithRedirect = "cmd /c \"" + pythonCmd + " 2>&1\"";
        
        FILE* pipe = _popen(cmdWithRedirect.c_str(), "r");
        if (!pipe) {
            std::cerr << "ERROR: Failed to start Python converter" << std::endl;
            return false;
        }
        
        char outputBuffer[256];
        std::string output;
        while (fgets(outputBuffer, sizeof(outputBuffer), pipe) != nullptr) {
            output += outputBuffer;
            std::cout << outputBuffer;  // 实时输出
        }
        
        int exitCode = _pclose(pipe);
        
        if (exitCode != 0) {
            std::cerr << "ERROR: Python converter failed with code " << exitCode << std::endl;
            std::cerr << "Output: " << output << std::endl;
            return false;
        }
#else
        int result = system(pythonCmd.c_str());
        if (result != 0) {
            std::cerr << "ERROR: Failed to convert OBJ to binary format" << std::endl;
            return false;
        }
#endif
        
        if (!std::filesystem::exists(tempPath)) {
            std::cerr << "ERROR: Converted file not found: " << tempPath << std::endl;
            return false;
        }
        
        loadPath = tempPath.string();
        std::cout << "Conversion complete: " << loadPath << std::endl;
    }
    
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

