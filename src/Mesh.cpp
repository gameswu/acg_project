#include "Mesh.h"
#include <glm/gtc/constants.hpp>

namespace ACG {

Mesh::Mesh() 
    : m_materialIndex(-1)
    , m_bboxMin(0.0f)
    , m_bboxMax(0.0f)
{
}

Mesh::~Mesh() {
}

void Mesh::SetVertices(const std::vector<Vertex>& vertices) {
    m_vertices = vertices;
}

void Mesh::SetIndices(const std::vector<uint32_t>& indices) {
    m_indices = indices;
}

std::shared_ptr<Mesh> Mesh::CreateSphere(float radius, int segments) {
    // TODO: Generate sphere mesh
    auto mesh = std::make_shared<Mesh>();
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreateBox(const glm::vec3& size) {
    // TODO: Generate box mesh
    auto mesh = std::make_shared<Mesh>();
    return mesh;
}

std::shared_ptr<Mesh> Mesh::CreatePlane(float width, float height) {
    // TODO: Generate plane mesh
    auto mesh = std::make_shared<Mesh>();
    return mesh;
}

void Mesh::ComputeTangents() {
    if (m_vertices.empty() || m_indices.empty()) {
        return;
    }
    
    // Initialize tangents to zero
    for (auto& vertex : m_vertices) {
        vertex.tangent = glm::vec3(0.0f);
    }
    
    // Calculate tangents for each triangle
    for (size_t i = 0; i < m_indices.size(); i += 3) {
        uint32_t i0 = m_indices[i];
        uint32_t i1 = m_indices[i + 1];
        uint32_t i2 = m_indices[i + 2];
        
        Vertex& v0 = m_vertices[i0];
        Vertex& v1 = m_vertices[i1];
        Vertex& v2 = m_vertices[i2];
        
        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;
        
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        if (std::isfinite(f)) {
            glm::vec3 tangent;
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
            
            // Accumulate tangents (averaging)
            v0.tangent += tangent;
            v1.tangent += tangent;
            v2.tangent += tangent;
        }
    }
    
    // Normalize and orthogonalize tangents (Gram-Schmidt)
    for (auto& vertex : m_vertices) {
        if (glm::length(vertex.tangent) > 0.0001f) {
            vertex.tangent = glm::normalize(vertex.tangent - vertex.normal * glm::dot(vertex.normal, vertex.tangent));
        }
    }
}

void Mesh::ComputeBoundingBox() {
    if (m_vertices.empty()) {
        m_bboxMin = glm::vec3(0.0f);
        m_bboxMax = glm::vec3(0.0f);
        return;
    }
    
    m_bboxMin = glm::vec3(std::numeric_limits<float>::max());
    m_bboxMax = glm::vec3(std::numeric_limits<float>::lowest());
    
    for (const auto& vertex : m_vertices) {
        m_bboxMin = glm::min(m_bboxMin, vertex.position);
        m_bboxMax = glm::max(m_bboxMax, vertex.position);
    }
}

} // namespace ACG
