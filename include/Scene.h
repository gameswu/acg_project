#pragma once

#include <vector>
#include <memory>
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
    
    // 加载场景
    bool LoadFromFile(const std::string& filename, bool useCustomMTLParser = true);
    
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

private:
    // Internal helper functions for mesh processing
    void ProcessNode(aiNode* node, const aiScene* scene);
    void ProcessMesh(aiMesh* mesh, const aiScene* scene);
    void CalculateBoundingBox();
    
    std::vector<std::shared_ptr<Mesh>> m_meshes;
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<Light>> m_lights;
    
    std::string m_name;
    glm::vec3 m_bboxMin;
    glm::vec3 m_bboxMax;
};

} // namespace ACG
