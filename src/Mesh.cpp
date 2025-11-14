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
    // TODO: Compute tangent vectors for normal mapping
}

void Mesh::ComputeBoundingBox() {
    // TODO: Compute bounding box from vertices
}

} // namespace ACG
