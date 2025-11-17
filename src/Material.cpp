#include "Material.h"

#include <glm/gtc/constants.hpp>

namespace ACG {

Material::Material()
    : m_type(MaterialType::Diffuse)
    , m_albedo(0.8f, 0.8f, 0.8f)
    , m_emission(0.0f, 0.0f, 0.0f)
    , m_specular(0.0f, 0.0f, 0.0f)
    , m_illum(2)
    , m_metallic(0.0f)
    , m_roughness(0.5f)
    , m_ior(1.45f)
    , m_transmission(0.0f)
    , m_subsurfaceRadius(0.0f)
    , m_subsurfaceColor(1.0f)
{
}

Material::~Material() {
}

glm::vec3 Material::Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal, const glm::vec2& texCoord) const {
    // Get albedo (with texture if available)
    glm::vec3 albedo = m_albedo;
    if (m_albedoTexture) {
        glm::vec4 texColor = m_albedoTexture->SampleBilinear(texCoord.x, texCoord.y);
        albedo = glm::vec3(texColor.r, texColor.g, texColor.b);
    }
    
    float NdotL = std::max(0.0f, glm::dot(normal, wi));
    float NdotV = std::max(0.0f, glm::dot(normal, wo));
    
    switch (m_type) {
        case MaterialType::Diffuse: {
            // Lambertian BRDF
            return albedo / glm::pi<float>();
        }
        
        case MaterialType::Specular: {
            // Cook-Torrance microfacet model
            glm::vec3 h = glm::normalize(wo + wi);
            float NdotH = std::max(0.0f, glm::dot(normal, h));
            float VdotH = std::max(0.0f, glm::dot(wo, h));
            
            // Get roughness (with texture if available)
            float roughness = m_roughness;
            if (m_roughnessTexture) {
                glm::vec4 texRoughness = m_roughnessTexture->SampleBilinear(texCoord.x, texCoord.y);
                roughness = texRoughness.r;
            }
            
            // GGX normal distribution
            float alpha = roughness * roughness;
            float alpha2 = alpha * alpha;
            float NdotH2 = NdotH * NdotH;
            float denom = NdotH2 * (alpha2 - 1.0f) + 1.0f;
            float D = alpha2 / (glm::pi<float>() * denom * denom);
            
            // Schlick-GGX geometry
            float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
            float G1_V = NdotV / (NdotV * (1.0f - k) + k);
            float G1_L = NdotL / (NdotL * (1.0f - k) + k);
            float G = G1_V * G1_L;
            
            // Fresnel (Schlick approximation)
            glm::vec3 F0 = glm::mix(glm::vec3(0.04f), albedo, m_metallic);
            glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * std::pow(1.0f - VdotH, 5.0f);
            
            // Specular BRDF
            glm::vec3 specular = (D * G * F) / std::max(4.0f * NdotV * NdotL, 0.001f);
            
            // Diffuse component for non-metallic
            glm::vec3 kD = (glm::vec3(1.0f) - F) * (1.0f - m_metallic);
            glm::vec3 diffuse = kD * albedo / glm::pi<float>();
            
            return diffuse + specular;
        }
        
        case MaterialType::Transmissive: {
            // Simplified glass BSDF
            glm::vec3 h = glm::normalize(wo + wi);
            float VdotH = std::max(0.0f, glm::dot(wo, h));
            
            // Fresnel for dielectric
            float F0 = ((1.0f - m_ior) / (1.0f + m_ior)) * ((1.0f - m_ior) / (1.0f + m_ior));
            float F = F0 + (1.0f - F0) * std::pow(1.0f - VdotH, 5.0f);
            
            // Reflection or refraction
            bool isReflection = glm::dot(wi, normal) > 0.0f;
            if (isReflection) {
                return glm::vec3(F) * albedo;
            } else {
                return glm::vec3(1.0f - F) * albedo;
            }
        }
        
        case MaterialType::Emissive:
            return m_emission;
        
        default:
            return albedo / glm::pi<float>();
    }
}

glm::vec3 Material::Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const {
    // Call the texture version with a dummy UV coordinate
    return Evaluate(wo, wi, normal, glm::vec2(0.0f));
}

glm::vec3 Material::Sample(const glm::vec3& wo, const glm::vec3& normal, glm::vec3& wi, float& pdf) const {
    // Build local coordinate system
    glm::vec3 tangent, bitangent;
    if (std::abs(normal.x) > 0.1f) {
        tangent = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal));
    } else {
        tangent = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), normal));
    }
    bitangent = glm::cross(normal, tangent);
    
    switch (m_type) {
        case MaterialType::Diffuse: {
            // Cosine-weighted hemisphere sampling
            float r1 = static_cast<float>(rand()) / RAND_MAX;
            float r2 = static_cast<float>(rand()) / RAND_MAX;
            
            float phi = 2.0f * glm::pi<float>() * r1;
            float cosTheta = std::sqrt(r2);
            float sinTheta = std::sqrt(1.0f - r2);
            
            glm::vec3 localDir = glm::vec3(
                std::cos(phi) * sinTheta,
                std::sin(phi) * sinTheta,
                cosTheta
            );
            
            wi = localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
            pdf = cosTheta / glm::pi<float>();
            return m_albedo / glm::pi<float>();
        }
        
        case MaterialType::Specular: {
            // GGX importance sampling
            float r1 = static_cast<float>(rand()) / RAND_MAX;
            float r2 = static_cast<float>(rand()) / RAND_MAX;
            
            float alpha = m_roughness * m_roughness;
            float alpha2 = alpha * alpha;
            
            float phi = 2.0f * glm::pi<float>() * r1;
            float cosTheta = std::sqrt((1.0f - r2) / (1.0f + (alpha2 - 1.0f) * r2));
            float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
            
            glm::vec3 h = glm::vec3(
                std::cos(phi) * sinTheta,
                std::sin(phi) * sinTheta,
                cosTheta
            );
            h = h.x * tangent + h.y * bitangent + h.z * normal;
            
            wi = glm::reflect(-wo, h);
            
            float NdotH = std::max(0.0f, cosTheta);
            float VdotH = std::max(0.0f, glm::dot(wo, h));
            
            // GGX PDF
            float denom = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
            float D = alpha2 / (glm::pi<float>() * denom * denom);
            pdf = D * NdotH / (4.0f * VdotH);
            
            return Evaluate(wo, wi, normal);
        }
        
        case MaterialType::Transmissive: {
            // Fresnel for glass
            float r = static_cast<float>(rand()) / RAND_MAX;
            float cosI = glm::dot(wo, normal);
            float F0 = ((1.0f - m_ior) / (1.0f + m_ior)) * ((1.0f - m_ior) / (1.0f + m_ior));
            float F = F0 + (1.0f - F0) * std::pow(1.0f - std::abs(cosI), 5.0f);
            
            if (r < F) {
                // Reflection
                wi = glm::reflect(-wo, normal);
                pdf = F;
            } else {
                // Refraction
                float eta = cosI > 0.0f ? 1.0f / m_ior : m_ior;
                wi = glm::refract(-wo, normal * glm::sign(cosI), eta);
                pdf = 1.0f - F;
            }
            
            return Evaluate(wo, wi, normal);
        }
        
        case MaterialType::Emissive:
            wi = normal;
            pdf = 1.0f;
            return m_emission;
        
        default:
            wi = normal;
            pdf = 1.0f;
            return m_albedo;
    }
}

float Material::PDF(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const {
    float NdotL = std::max(0.0f, glm::dot(normal, wi));
    
    switch (m_type) {
        case MaterialType::Diffuse:
            return NdotL / glm::pi<float>();
        
        case MaterialType::Specular: {
            glm::vec3 h = glm::normalize(wo + wi);
            float NdotH = std::max(0.0f, glm::dot(normal, h));
            float VdotH = std::max(0.0f, glm::dot(wo, h));
            
            float alpha = m_roughness * m_roughness;
            float alpha2 = alpha * alpha;
            float denom = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
            float D = alpha2 / (glm::pi<float>() * denom * denom);
            
            return D * NdotH / (4.0f * VdotH);
        }
        
        case MaterialType::Transmissive: {
            float cosI = glm::dot(wo, normal);
            float F0 = ((1.0f - m_ior) / (1.0f + m_ior)) * ((1.0f - m_ior) / (1.0f + m_ior));
            float F = F0 + (1.0f - F0) * std::pow(1.0f - std::abs(cosI), 5.0f);
            
            bool isReflection = glm::dot(wi, normal) > 0.0f;
            return isReflection ? F : (1.0f - F);
        }
        
        default:
            return 1.0f;
    }
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
