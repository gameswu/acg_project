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
 * Implements Wavefront MTL material specification (v4.2, October 1995)
 * 
 * Supported MTL properties:
 * - Ka: Ambient reflectivity (RGB 0.0-1.0)
 * - Kd: Diffuse reflectivity (RGB 0.0-1.0)
 * - Ks: Specular reflectivity (RGB 0.0-1.0)
 * - Ke: Emissive color (RGB, typically 0.0-1.0+)
 * - Tf: Transmission filter (RGB 0.0-1.0)
 * - Ns: Specular exponent (0-1000, focus of highlight)
 * - Ni: Optical density / Index of Refraction (0.001-10)
 * - d:  Dissolve factor (0.0-1.0, 1.0=opaque)
 * - illum: Illumination model (0-10)
 * 
 * Illumination Models (from MTL spec):
 * 0  = Flat color (no lighting)
 * 1  = Diffuse (Lambertian)
 * 2  = Diffuse + Specular (Blinn-Phong) [most common]
 * 3  = Reflection (ray traced)
 * 4  = Glass (transparency + reflection)
 * 5  = Fresnel Mirror (perfect reflection)
 * 6  = Refraction (Fresnel off)
 * 7  = Refraction + Fresnel (realistic glass)
 * 8  = Reflection (no ray trace)
 * 9  = Glass (no ray trace)
 * 10 = Shadow matte
 */
class Material {
public:
    Material();
    virtual ~Material();

    // Basic properties
    void SetType(MaterialType type) { m_type = type; }
    MaterialType GetType() const { return m_type; }
    
    // MTL color properties (Ka, Kd, Ks, Ke, Tf)
    void SetAmbient(const glm::vec3& ambient) { m_ambient = ambient; }
    void SetAlbedo(const glm::vec3& albedo) { m_albedo = albedo; }  // Kd
    void SetSpecular(const glm::vec3& specular) { m_specular = specular; }  // Ks
    void SetEmission(const glm::vec3& emission) { m_emission = emission; }  // Ke
    void SetTransmissionFilter(const glm::vec3& tf) { m_transmissionFilter = tf; }  // Tf
    
    glm::vec3 GetAmbient() const { return m_ambient; }
    glm::vec3 GetAlbedo() const { return m_albedo; }
    glm::vec3 GetSpecular() const { return m_specular; }
    glm::vec3 GetEmission() const { return m_emission; }
    glm::vec3 GetTransmissionFilter() const { return m_transmissionFilter; }
    
    // MTL scalar properties
    void SetSpecularExponent(float ns) { m_specularExponent = ns; }  // Ns (0-1000)
    void SetDissolve(float d) { m_dissolve = d; }  // d (0.0-1.0, 1.0=opaque)
    void SetOpticalDensity(float ni) { m_opticalDensity = ni; }  // Ni (IOR)
    void SetIllum(int illum) { m_illum = illum; }  // illum (0-10)
    
    float GetSpecularExponent() const { return m_specularExponent; }
    float GetDissolve() const { return m_dissolve; }
    float GetOpticalDensity() const { return m_opticalDensity; }
    int GetIllum() const { return m_illum; }
    
    // PBR-converted properties (for shader compatibility)
    void SetMetallic(float metallic) { m_metallic = metallic; }
    void SetRoughness(float roughness) { m_roughness = roughness; }
    void SetIOR(float ior) { m_ior = ior; }  // Alias for Ni
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
    
    std::shared_ptr<Texture> GetAlbedoTexture() const { return m_albedoTexture; }
    
    // Evaluate material
    virtual glm::vec3 Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const;
    virtual glm::vec3 Evaluate(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal, const glm::vec2& texCoord) const;
    virtual glm::vec3 Sample(const glm::vec3& wo, const glm::vec3& normal, glm::vec3& wi, float& pdf) const;
    virtual float PDF(const glm::vec3& wo, const glm::vec3& wi, const glm::vec3& normal) const;

protected:
    MaterialType m_type;
    
    // MTL Specification color properties
    glm::vec3 m_ambient;           // Ka: Ambient reflectivity (RGB 0.0-1.0)
    glm::vec3 m_albedo;            // Kd: Diffuse reflectivity (RGB 0.0-1.0)
    glm::vec3 m_specular;          // Ks: Specular reflectivity (RGB 0.0-1.0)
    glm::vec3 m_emission;          // Ke: Emissive color (RGB 0.0+)
    glm::vec3 m_transmissionFilter; // Tf: Transmission filter (RGB 0.0-1.0)
    
    // MTL Specification scalar properties
    float m_specularExponent;      // Ns: Specular exponent (0-1000)
    float m_dissolve;              // d: Dissolve/opacity (0.0-1.0, 1.0=opaque)
    float m_opticalDensity;        // Ni: Index of refraction (0.001-10)
    int m_illum;                   // illum: Illumination model (0-10)
    
    // PBR-converted properties (derived from MTL for shader use)
    float m_metallic;              // Metallic factor (0.0-1.0)
    float m_roughness;             // Roughness factor (0.0-1.0)
    float m_ior;                   // Index of refraction (alias for Ni)
    float m_transmission;          // Transmission amount (derived from d)
    
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
