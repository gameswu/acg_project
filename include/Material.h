#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <cstdint>
#include "Texture.h"
#include "MaterialLayers.h"

namespace ACG {

/**
 * @brief GPU-aligned Material Structure (64 bytes)
 * 
 * Implements a dynamic layered material system supporting:
 * - Base PBR properties (metallic-roughness workflow)
 * - Optional extended layers (clearcoat, transmission, sheen, etc.)
 * - Virtual texture support
 * - Backward compatible with MTL format
 * 
 * Memory Layout: 64 bytes total (cache-line friendly)
 * - Base properties: 32 bytes
 * - Texture indices: 16 bytes
 * - Layer info: 16 bytes
 * 
 * Extended layers are stored separately and accessed via extendedDataIndex.
 */
struct MaterialData {
    // Base PBR properties (32 bytes = 4 vec4s)
    glm::vec4 baseColor_metallic;  // RGB diffuse color / albedo, W = metallic [0,1]
    glm::vec4 emission_roughness;  // RGB emission (HDR), W = roughness [0,1]
    glm::vec4 ior_opacity_flags_idx; // X=IOR, Y=opacity, Z=layerFlags(as float), W=extendedDataIndex(as float)
    glm::vec4 texIndices;          // X=baseColorTex, Y=normalTex, Z=metallicRoughnessTex, W=emissionTex (all as floats, cast to int)
    
    // Constructor
    MaterialData() 
        : baseColor_metallic(0.8f, 0.8f, 0.8f, 0.0f),
          emission_roughness(0.0f, 0.0f, 0.0f, 0.5f),
          ior_opacity_flags_idx(1.5f, 1.0f, 0.0f, 0.0f),
          texIndices(-1.0f, -1.0f, -1.0f, -1.0f) {}
          
    // Helper accessors
    glm::vec3 GetBaseColor() const { return glm::vec3(baseColor_metallic); }
    float GetMetallic() const { return baseColor_metallic.w; }
    glm::vec3 GetEmission() const { return glm::vec3(emission_roughness); }
    float GetRoughness() const { return emission_roughness.w; }
    float GetIOR() const { return ior_opacity_flags_idx.x; }
    float GetOpacity() const { return ior_opacity_flags_idx.y; }
    uint32_t GetLayerFlags() const { 
        uint32_t flags; 
        std::memcpy(&flags, &ior_opacity_flags_idx.z, sizeof(uint32_t)); 
        return flags; 
    }
    uint32_t GetExtendedDataIndex() const { 
        uint32_t idx; 
        std::memcpy(&idx, &ior_opacity_flags_idx.w, sizeof(uint32_t)); 
        return idx; 
    }
};
static_assert(sizeof(MaterialData) == 64, "MaterialData must be exactly 64 bytes");

/**
 * @brief Material Class (CPU-side representation)
 * 
 * High-level material interface with MTL compatibility.
 * Converts to MaterialData for GPU upload.
 */
class Material {
public:
    Material();
    virtual ~Material();

    // Name
    void SetName(const std::string& name) { m_name = name; }
    std::string GetName() const { return m_name; }

    // Basic PBR properties
    void SetBaseColor(const glm::vec3& color) { m_baseColor = color; }
    void SetMetallic(float metallic) { m_metallic = metallic; }
    void SetRoughness(float roughness) { m_roughness = roughness; }
    void SetEmission(const glm::vec3& emission) { m_emission = emission; }
    void SetIOR(float ior) { m_ior = ior; }
    void SetOpacity(float opacity) { m_opacity = opacity; }
    
    glm::vec3 GetBaseColor() const { return m_baseColor; }
    float GetMetallic() const { return m_metallic; }
    float GetRoughness() const { return m_roughness; }
    glm::vec3 GetEmission() const { return m_emission; }
    float GetIOR() const { return m_ior; }
    float GetOpacity() const { return m_opacity; }
    
    // Legacy MTL compatibility (maps to PBR)
    void SetAlbedo(const glm::vec3& albedo) { m_baseColor = albedo; }  // Kd -> baseColor
    void SetSpecular(const glm::vec3& specular);  // Ks -> metallic conversion
    void SetSpecularExponent(float ns);  // Ns -> roughness conversion
    void SetDissolve(float d) { m_opacity = d; }  // d -> opacity
    void SetOpticalDensity(float ni) { m_ior = ni; }  // Ni -> IOR
    void SetTransmissionFilter(const glm::vec3& tf);  // Tf -> transmission layer
    void SetIllum(int illum);  // illum -> layer flags + properties
    
    glm::vec3 GetAlbedo() const { return m_baseColor; }
    float GetDissolve() const { return m_opacity; }
    float GetOpticalDensity() const { return m_ior; }
    
    // Extended layers
    bool HasLayer(MaterialLayerFlags layer) const { return (m_layerFlags & layer) != 0; }
    void AddLayer(MaterialLayerFlags layer) { m_layerFlags |= layer; }
    void RemoveLayer(MaterialLayerFlags layer) { m_layerFlags &= ~layer; }
    uint32_t GetLayerFlags() const { return m_layerFlags; }
    
    void SetClearcoatLayer(const ClearcoatLayer& layer);
    void SetTransmissionLayer(const TransmissionLayer& layer);
    void SetSheenLayer(const SheenLayer& layer);
    void SetSubsurfaceLayer(const SubsurfaceLayer& layer);
    void SetAnisotropyLayer(const AnisotropyLayer& layer);
    void SetIridescenceLayer(const IridescenceLayer& layer);
    void SetVolumeLayer(const VolumeLayer& layer);
    
    const ClearcoatLayer* GetClearcoatLayer() const;
    const TransmissionLayer* GetTransmissionLayer() const;
    const SheenLayer* GetSheenLayer() const;
    const SubsurfaceLayer* GetSubsurfaceLayer() const;
    const AnisotropyLayer* GetAnisotropyLayer() const;
    const IridescenceLayer* GetIridescenceLayer() const;
    const VolumeLayer* GetVolumeLayer() const;
    
    // Textures
    void SetBaseColorTexture(std::shared_ptr<Texture> texture, int32_t texIdx = -1);
    void SetNormalTexture(std::shared_ptr<Texture> texture, int32_t texIdx = -1);
    void SetMetallicRoughnessTexture(std::shared_ptr<Texture> texture, int32_t texIdx = -1);
    void SetEmissionTexture(std::shared_ptr<Texture> texture, int32_t texIdx = -1);
    
    std::shared_ptr<Texture> GetBaseColorTexture() const { return m_baseColorTexture; }
    std::shared_ptr<Texture> GetNormalTexture() const { return m_normalTexture; }
    std::shared_ptr<Texture> GetMetallicRoughnessTexture() const { return m_metallicRoughnessTexture; }
    std::shared_ptr<Texture> GetEmissionTexture() const { return m_emissionTexture; }
    
    int32_t GetBaseColorTexIdx() const { return m_baseColorTexIdx; }
    int32_t GetNormalTexIdx() const { return m_normalTexIdx; }
    int32_t GetMetallicRoughnessTexIdx() const { return m_metallicRoughnessTexIdx; }
    int32_t GetEmissionTexIdx() const { return m_emissionTexIdx; }
    
    // Virtual texture support
    void SetTextureSize(const glm::vec2& size) { m_textureSize = size; }
    glm::vec2 GetTextureSize() const { return m_textureSize; }
    
    // Convert to GPU data structure
    MaterialData ToGPUData() const;
    
    // Get extended layer data for GPU upload
    const std::vector<MaterialExtendedData>& GetExtendedLayers() const { return m_extendedLayers; }
    uint32_t GetExtendedDataBaseIndex() const { return m_extendedDataBaseIndex; }
    void SetExtendedDataBaseIndex(uint32_t index) { m_extendedDataBaseIndex = index; }

protected:
    // Name
    std::string m_name;
    
    // Base PBR properties
    glm::vec3 m_baseColor;
    float m_metallic;
    glm::vec3 m_emission;
    float m_roughness;
    float m_ior;
    float m_opacity;
    
    // Layer management
    uint32_t m_layerFlags;
    std::vector<MaterialExtendedData> m_extendedLayers;  // CPU-side layer storage
    uint32_t m_extendedDataBaseIndex;  // GPU buffer offset (set by Scene)
    
    // Texture references and indices
    std::shared_ptr<Texture> m_baseColorTexture;
    std::shared_ptr<Texture> m_normalTexture;
    std::shared_ptr<Texture> m_metallicRoughnessTexture;
    std::shared_ptr<Texture> m_emissionTexture;
    
    int32_t m_baseColorTexIdx;
    int32_t m_normalTexIdx;
    int32_t m_metallicRoughnessTexIdx;
    int32_t m_emissionTexIdx;
    
    // Virtual texture
    glm::vec2 m_textureSize;
};

} // namespace ACG
