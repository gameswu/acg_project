#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include "Mesh.h"
#include "Material.h"
#include "Light.h"

// Forward declarations for Assimp types (geometry loading only)
struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;

namespace ACG {

/**
 * @brief Loading progress callback
 * @param stage Current loading stage name
 * @param current Current progress value
 * @param total Total progress value
 */
using LoadProgressCallback = std::function<void(const std::string& stage, int current, int total)>;

/**
 * @brief Scene loading configuration
 */
struct SceneLoadConfig {
    bool useCustomMTLParser = true;
    bool enableBatchLoading = true;      // Enable batch loading for large scenes
    int maxMeshesPerBatch = 500;         // Max meshes per batch (0 = unlimited)
    int maxTexturesPerBatch = 64;        // Max textures per batch
    size_t maxMemoryMB = 4096;           // Max memory usage in MB (0 = unlimited)
    LoadProgressCallback progressCallback = nullptr;
};

/**
 * @brief Scene container holding all geometry, materials, and lights
 */
class Scene {
public:
    Scene();
    ~Scene();

    // 添加场景元素
    void AddMesh(std::shared_ptr<Mesh> mesh);
    void AddMaterial(std::shared_ptr<Material> material);
    void AddLight(std::shared_ptr<Light> light);
    
    // 加载场景 - 支持分批加载配置
    bool LoadFromFile(const std::string& filename, bool useCustomMTLParser = true);
    bool LoadFromFileEx(const std::string& filename, const SceneLoadConfig& config);
    
    // 获取场景数据
    const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const { return m_meshes; }
    const std::vector<std::shared_ptr<Material>>& GetMaterials() const { return m_materials; }
    const std::vector<std::shared_ptr<Light>>& GetLights() const { return m_lights; }
    
    // 场景名称
    std::string GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }
    
    // 场景包围盒
    void ComputeBoundingBox();
    glm::vec3 GetBBoxMin() const { return m_bboxMin; }
    glm::vec3 GetBBoxMax() const { return m_bboxMax; }
    
    // 获取加载统计信息
    struct LoadStats {
        int totalMeshes = 0;
        int totalTriangles = 0;
        int totalVertices = 0;
        int totalMaterials = 0;
        int totalTextures = 0;
        size_t estimatedMemoryMB = 0;
    };
    const LoadStats& GetLoadStats() const { return m_loadStats; }

private:
    // Internal helper functions for mesh processing
    void ProcessNode(aiNode* node, const aiScene* scene);
    void ProcessMesh(aiMesh* mesh, const aiScene* scene);
    void CalculateBoundingBox();
    
    // Batch loading helpers
    bool LoadGeometryBatched(const aiScene* scene, const SceneLoadConfig& config);
    void EstimateMemoryUsage();
    
    std::vector<std::shared_ptr<Mesh>> m_meshes;
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<Light>> m_lights;
    
    std::string m_name;
    glm::vec3 m_bboxMin;
    glm::vec3 m_bboxMax;
    
    LoadStats m_loadStats;
};

} // namespace ACG
