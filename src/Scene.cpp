#include "Scene.h"
#include <iostream>
#include <limits>

namespace ACG {

Scene::Scene() 
    : m_bboxMin(std::numeric_limits<float>::max())
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
    // TODO: Load scene from file (using Assimp or custom format)
    std::cout << "Loading scene from: " << filename << std::endl;
    return true;
}

void Scene::ComputeBoundingBox() {
    // TODO: Compute scene bounding box from all meshes
    m_bboxMin = glm::vec3(-10.0f);
    m_bboxMax = glm::vec3(10.0f);
}

} // namespace ACG
