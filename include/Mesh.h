#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace ACG {

/**
 * @brief Triangle mesh data structure
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;
};

class Mesh {
public:
    Mesh();
    ~Mesh();

    // 设置网格数据
    void SetVertices(const std::vector<Vertex>& vertices);
    void SetIndices(const std::vector<uint32_t>& indices);
    void SetMaterialIndex(int index) { m_materialIndex = index; }
    void SetName(const std::string& name) { m_name = name; }
    
    // 获取网格数据
    const std::vector<Vertex>& GetVertices() const { return m_vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_indices; }
    int GetMaterialIndex() const { return m_materialIndex; }
    std::string GetName() const { return m_name; }
    
    // 生成基本几何体
    static std::shared_ptr<Mesh> CreateSphere(float radius, int segments);
    static std::shared_ptr<Mesh> CreateBox(const glm::vec3& size);
    static std::shared_ptr<Mesh> CreatePlane(float width, float height);
    
    // 计算切线空间（用于法线贴图）
    void ComputeTangents();
    
    // 计算包围盒
    void ComputeBoundingBox();
    glm::vec3 GetBBoxMin() const { return m_bboxMin; }
    glm::vec3 GetBBoxMax() const { return m_bboxMax; }

private:
    std::string m_name;
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    int m_materialIndex;
    
    glm::vec3 m_bboxMin;
    glm::vec3 m_bboxMax;
};

} // namespace ACG
