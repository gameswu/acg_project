#pragma once

#include <glm/glm.hpp>
#include <memory>
#include "Texture.h"

namespace ACG {

enum class MaterialType {
    Diffuse,
    Specular,
    Transmissive,      // Glass, water
    PrincipledBSDF,    // Disney principled BRDF
    Emissive,
    Subsurface         // Subsurface scattering
};

/**
 * @brief Material properties for path tracing
 * 
 * Supports various material types including:
 * - Diffuse (Lambertian)
 * - Specular (Mirror, metal)
 * - Transmissive (Glass, refractive)
 * - Principled BSDF (Disney PBR model)
 * - Subsurface scattering (for skin, wax, etc.)
 */
class Material {
public:
    Material();
    virtual ~Material();

    // Basic properties
    void SetType(MaterialType type) { m_type = type; }
    MaterialType GetType() const { return m_type; }
    
    // Color properties
    void SetAlbedo(const glm::vec3& albedo) { m_albedo = albedo; }
    void SetEmission(const glm::vec3& emission) { m_emission = emission; }
    glm::vec3 GetAlbedo() const { return m_albedo; }
    glm::vec3 GetEmission() const { return m_emission; }
    
    // Physical properties
    void SetMetallic(float metallic) { m_metallic = metallic; }
    void SetRoughness(float roughness) { m_roughness = roughness; }
    void SetIOR(float ior) { m_ior = ior; }  // Index of refraction
    void SetTransmission(float transmission) { m_transmission = transmission; }
    float GetMetallic() const { return m_metallic; }
    float GetRoughness() const { return m_roughness; }
    float GetIOR() const { return m_ior; }
    float GetTransmission() const { return m_transmission; }
    
    // Subsurface scattering
    void SetSubsurfaceRadius(const glm::vec3& radius) { m_subsurfaceRadius = radius; }
    void SetSubsurfaceColor(const glm::vec3& color) { m_subsurfaceColor = color; }
    
    // Textures
    void SetAlbedoTexture(std::shared_ptr<Texture> texture) { m_albedoTexture = texture; }
    void SetNormalTexture(std::shared_ptr<Texture> texture) { m_normalTexture = texture; }
    void SetRoughnessTexture(std::shared_ptr<Texture> texture) { m_roughnessTexture = texture; }
    void SetMetallicTexture(std::shared_ptr<Texture> texture) { m_metallicTexture = texture; }
    
    // Evaluate material
    virtual glm::vec3 Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const;
    virtual glm::vec3 Sample(const glm::vec3& wo, const glm::vec3& normal, glm::vec3& wi, float& pdf) const;
    virtual float PDF(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const;

protected:
    MaterialType m_type;
    
    // Base color
    glm::vec3 m_albedo;
    glm::vec3 m_emission;
    
    // PBR parameters
    float m_metallic;
    float m_roughness;
    float m_ior;
    float m_transmission;
    
    // Subsurface scattering
    glm::vec3 m_subsurfaceRadius;
    glm::vec3 m_subsurfaceColor;
    
    // Textures
    std::shared_ptr<Texture> m_albedoTexture;
    std::shared_ptr<Texture> m_normalTexture;
    std::shared_ptr<Texture> m_roughnessTexture;
    std::shared_ptr<Texture> m_metallicTexture;
};

// Specialized material types

class DiffuseMaterial : public Material {
public:
    DiffuseMaterial(const glm::vec3& albedo);
};

class SpecularMaterial : public Material {
public:
    SpecularMaterial(const glm::vec3& albedo, float roughness);
};

class TransmissiveMaterial : public Material {
public:
    TransmissiveMaterial(const glm::vec3& albedo, float ior);
};

class PrincipledBSDFMaterial : public Material {
public:
    PrincipledBSDFMaterial();
};

} // namespace ACG
