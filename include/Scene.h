#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include "Mesh.h"
#include "Material.h"
#include "Light.h"

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
    
    // 设置场景数据（用于Python加载器）
    void SetMeshes(const std::vector<std::shared_ptr<Mesh>>& meshes) { m_meshes = meshes; }
    void SetMaterials(const std::vector<std::shared_ptr<Material>>& materials) { m_materials = materials; }
    
    // 加载场景 - 使用Python加载器
    bool LoadFromFile(const std::string& filename);
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
    
    // 材质层管理 (新增)
    void CollectAllMaterialLayers();
    const std::vector<MaterialExtendedData>& GetMaterialLayers() const { return m_materialLayers; }
    uint32_t AddMaterialLayer(const MaterialExtendedData& layer);
    
    // 获取加载统计信息
    struct LoadStats {
        int totalMeshes = 0;
        int totalTriangles = 0;
        int totalVertices = 0;
        int totalMaterials = 0;
        int totalTextures = 0;
        size_t estimatedMemoryMB = 0;
        int totalMaterialLayers = 0;  // 新增
    };
    const LoadStats& GetLoadStats() const { return m_loadStats; }

private:
    void CalculateBoundingBox();
    void EstimateMemoryUsage();
    
    std::vector<std::shared_ptr<Mesh>> m_meshes;
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<Light>> m_lights;
    
    // 材质层数据 (新增)
    std::vector<MaterialExtendedData> m_materialLayers;
    
    std::string m_name;
    glm::vec3 m_bboxMin;
    glm::vec3 m_bboxMax;
    
    LoadStats m_loadStats;
};

} // namespace ACG

