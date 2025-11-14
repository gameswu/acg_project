#include "Material.h"

namespace ACG {

Material::Material()
    : m_type(MaterialType::Diffuse)
    , m_albedo(0.8f, 0.8f, 0.8f)
    , m_emission(0.0f, 0.0f, 0.0f)
    , m_metallic(0.0f)
    , m_roughness(0.5f)
    , m_ior(1.5f)
    , m_transmission(0.0f)
    , m_subsurfaceRadius(0.0f)
    , m_subsurfaceColor(1.0f)
{
}

Material::~Material() {
}

glm::vec3 Material::Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const {
    // TODO: Implement BRDF/BSDF evaluation based on material type
    return m_albedo;
}

glm::vec3 Material::Sample(const glm::vec3& wo, const glm::vec3& normal, glm::vec3& wi, float& pdf) const {
    // TODO: Implement importance sampling for material
    wi = normal;  // Placeholder
    pdf = 1.0f;
    return m_albedo;
}

float Material::PDF(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const {
    // TODO: Calculate PDF for given directions
    return 1.0f;
}

// Specialized materials

DiffuseMaterial::DiffuseMaterial(const glm::vec3& albedo) {
    m_type = MaterialType::Diffuse;
    m_albedo = albedo;
}

SpecularMaterial::SpecularMaterial(const glm::vec3& albedo, float roughness) {
    m_type = MaterialType::Specular;
    m_albedo = albedo;
    m_roughness = roughness;
}

TransmissiveMaterial::TransmissiveMaterial(const glm::vec3& albedo, float ior) {
    m_type = MaterialType::Transmissive;
    m_albedo = albedo;
    m_ior = ior;
    m_transmission = 1.0f;
}

PrincipledBSDFMaterial::PrincipledBSDFMaterial() {
    m_type = MaterialType::PrincipledBSDF;
    // TODO: Set up default Disney principled BRDF parameters
}

} // namespace ACG
