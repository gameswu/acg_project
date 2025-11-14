#include "Light.h"
#include <iostream>

namespace ACG {

Light::Light(LightType type)
    : m_type(type)
    , m_color(1.0f, 1.0f, 1.0f)
    , m_intensity(1.0f)
{
}

Light::~Light() {
}

// PointLight implementation

PointLight::PointLight()
    : Light(LightType::Point)
    , m_position(0.0f, 10.0f, 0.0f)
{
}

glm::vec3 PointLight::Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const {
    // TODO: Sample point light
    lightDir = glm::normalize(m_position - hitPoint);
    distance = glm::length(m_position - hitPoint);
    pdf = 1.0f;
    
    // Inverse square falloff
    float attenuation = 1.0f / (distance * distance);
    return m_color * m_intensity * attenuation;
}

float PointLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    return 1.0f;  // Delta distribution
}

// AreaLight implementation

AreaLight::AreaLight()
    : Light(LightType::Area)
    , m_position(0.0f, 10.0f, 0.0f)
    , m_normal(0.0f, -1.0f, 0.0f)
    , m_size(2.0f, 2.0f)
{
}

glm::vec3 AreaLight::Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const {
    // TODO: Sample area light uniformly
    // Generate random point on rectangular light
    lightDir = glm::normalize(m_position - hitPoint);
    distance = glm::length(m_position - hitPoint);
    pdf = 1.0f;
    
    return m_color * m_intensity;
}

float AreaLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    // TODO: Calculate PDF for area light
    return 0.0f;
}

// EnvironmentLight implementation

EnvironmentLight::EnvironmentLight()
    : Light(LightType::Environment)
    , m_width(0)
    , m_height(0)
{
}

bool EnvironmentLight::LoadHDR(const std::string& filename) {
    // TODO: Load HDR image (use stb_image with HDR support)
    // stbi_loadf() for HDR images
    std::cout << "Loading HDR environment: " << filename << std::endl;
    
    // Build importance sampling structures
    // BuildCDF();
    
    return false;
}

glm::vec3 EnvironmentLight::Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const {
    // TODO: Importance sample environment map using CDF
    lightDir = glm::vec3(0.0f, 1.0f, 0.0f);
    distance = std::numeric_limits<float>::max();
    pdf = 1.0f;
    
    return Evaluate(lightDir);
}

float EnvironmentLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    // TODO: Calculate PDF based on importance sampling
    return 0.0f;
}

glm::vec3 EnvironmentLight::Evaluate(const glm::vec3& direction) const {
    // TODO: Sample HDR environment map in given direction
    // Convert direction to spherical coordinates (theta, phi)
    // Sample texture at (theta, phi)
    return m_color * m_intensity;
}

void EnvironmentLight::BuildCDF() {
    // TODO: Build cumulative distribution function for importance sampling
    // This enables efficient sampling of bright areas in the environment map
}

} // namespace ACG
