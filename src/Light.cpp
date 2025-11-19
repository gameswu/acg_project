#include "Light.h"
#include <iostream>
#include <algorithm>

#include "stb_image.h"
#include <glm/gtc/constants.hpp>

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
    glm::vec3 toLight = m_position - hitPoint;
    distance = glm::length(toLight);
    lightDir = toLight / distance;  // Normalize
    pdf = 1.0f;  // Delta distribution for point light
    
    // Inverse square falloff
    float attenuation = 1.0f / (distance * distance + 0.0001f);  // Small epsilon to avoid division by zero
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
    // Generate random point on rectangular light (uniform sampling)
    // In practice, would use random numbers here
    // For now, sample the center
    glm::vec3 tangent = glm::normalize(glm::cross(m_normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (glm::length(tangent) < 0.001f) {
        tangent = glm::normalize(glm::cross(m_normal, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    glm::vec3 bitangent = glm::cross(m_normal, tangent);
    
    // Sample point on rectangle (would be random in real implementation)
    float u = 0.5f;  // Placeholder: should be random [0,1]
    float v = 0.5f;  // Placeholder: should be random [0,1]
    glm::vec3 samplePoint = m_position + (u - 0.5f) * m_size.x * tangent + (v - 0.5f) * m_size.y * bitangent;
    
    glm::vec3 toLight = samplePoint - hitPoint;
    distance = glm::length(toLight);
    lightDir = toLight / distance;
    
    // Calculate PDF (solid angle conversion)
    float area = m_size.x * m_size.y;
    float cosTheta = std::max(0.0f, glm::dot(-lightDir, m_normal));
    pdf = (distance * distance) / (area * cosTheta + 0.0001f);
    
    return m_color * m_intensity * cosTheta;
}

float AreaLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    // Calculate intersection with light plane
    float denom = glm::dot(lightDir, m_normal);
    if (denom >= 0.0f) {  // Light is facing away
        return 0.0f;
    }
    
    float t = glm::dot(m_position - hitPoint, m_normal) / denom;
    if (t <= 0.0f) {
        return 0.0f;
    }
    
    glm::vec3 hitPos = hitPoint + lightDir * t;
    glm::vec3 localPos = hitPos - m_position;
    
    // Check if hit point is within light bounds
    glm::vec3 tangent = glm::normalize(glm::cross(m_normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    if (glm::length(tangent) < 0.001f) {
        tangent = glm::normalize(glm::cross(m_normal, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    glm::vec3 bitangent = glm::cross(m_normal, tangent);
    
    float u = glm::dot(localPos, tangent);
    float v = glm::dot(localPos, bitangent);
    
    if (std::abs(u) > m_size.x * 0.5f || std::abs(v) > m_size.y * 0.5f) {
        return 0.0f;
    }
    
    float area = m_size.x * m_size.y;
    float distance = t;
    float cosTheta = std::abs(denom);
    
    return (distance * distance) / (area * cosTheta + 0.0001f);
}

// EnvironmentLight implementation

EnvironmentLight::EnvironmentLight()
    : Light(LightType::Environment)
    , m_width(0)
    , m_height(0)
{
}

// DirectionalLight implementation

DirectionalLight::DirectionalLight()
    : Light(LightType::Directional)
    , m_direction(0.0f, 1.0f, 0.0f)
{
}

glm::vec3 DirectionalLight::Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const {
    // Directional light is at infinity. lightDir points from surface toward the light.
    lightDir = glm::normalize(m_direction);
    distance = std::numeric_limits<float>::infinity();
    pdf = 1.0f; // Delta distribution (handled specially by integrator)

    // Return constant radiance (no attenuation)
    return m_color * m_intensity;
}

float DirectionalLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    // Delta-directional light -> PDF is zero except at exact direction; return large value or 0.
    return 0.0f;
}

bool EnvironmentLight::LoadHDR(const std::string& filename) {
    std::cout << "Loading HDR environment: " << filename << std::endl;
    
    int width, height, channels;
    float* data = stbi_loadf(filename.c_str(), &width, &height, &channels, 3);
    
    if (!data) {
        std::cerr << "Failed to load HDR: " << filename << std::endl;
        return false;
    }
    
    m_width = width;
    m_height = height;
    m_data.resize(width * height);
    
    for (int i = 0; i < width * height; ++i) {
        m_data[i] = glm::vec3(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
    }
    
    stbi_image_free(data);
    
    // Build importance sampling CDF
    BuildCDF();
    
    std::cout << "Loaded HDR: " << width << "x" << height << std::endl;
    return true;
}

glm::vec3 EnvironmentLight::Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const {
    // Uniform sphere sampling (simplified - importance sampling would use CDF)
    // In practice, would sample based on luminance distribution
    float u1 = 0.5f;  // Placeholder: should be random [0,1]
    float u2 = 0.5f;  // Placeholder: should be random [0,1]
    
    float theta = std::acos(1.0f - 2.0f * u1);
    float phi = 2.0f * glm::pi<float>() * u2;
    
    lightDir = glm::vec3(
        std::sin(theta) * std::cos(phi),
        std::cos(theta),
        std::sin(theta) * std::sin(phi)
    );
    
    distance = std::numeric_limits<float>::max();
    pdf = 1.0f / (4.0f * glm::pi<float>());  // Uniform sphere
    
    return Evaluate(lightDir);
}

float EnvironmentLight::PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const {
    if (m_data.empty()) {
        return 1.0f / (4.0f * glm::pi<float>());
    }
    // Uniform sphere PDF (importance sampling would compute based on CDF)
    return 1.0f / (4.0f * glm::pi<float>());
}

glm::vec3 EnvironmentLight::Evaluate(const glm::vec3& direction) const {
    if (m_data.empty()) {
        return m_color * m_intensity;
    }
    
    // Convert direction to spherical coordinates
    float theta = std::acos(std::clamp(direction.y, -1.0f, 1.0f));
    float phi = std::atan2(direction.z, direction.x);
    if (phi < 0.0f) phi += 2.0f * glm::pi<float>();
    
    // Convert to UV coordinates
    float u = phi / (2.0f * glm::pi<float>());
    float v = theta / glm::pi<float>();
    
    // Sample environment map
    int x = static_cast<int>(u * m_width) % m_width;
    int y = static_cast<int>(v * m_height) % m_height;
    int idx = y * m_width + x;
    
    return m_data[idx] * m_intensity;
}

void EnvironmentLight::BuildCDF() {
    if (m_data.empty()) {
        return;
    }
    
    // Build CDF for importance sampling
    m_cdf.resize(m_width * m_height + 1);
    m_cdf[0] = 0.0f;
    
    for (int i = 0; i < m_width * m_height; ++i) {
        // Calculate luminance of pixel
        float luminance = 0.2126f * m_data[i].r + 0.7152f * m_data[i].g + 0.0722f * m_data[i].b;
        // Account for solid angle (sin(theta) weighting)
        int y = i / m_width;
        float theta = (y + 0.5f) * glm::pi<float>() / m_height;
        float sinTheta = std::sin(theta);
        m_cdf[i + 1] = m_cdf[i] + luminance * sinTheta;
    }
    
    // Normalize CDF
    float total = m_cdf.back();
    if (total > 0.0f) {
        for (auto& val : m_cdf) {
            val /= total;
        }
    }
}

} // namespace ACG
