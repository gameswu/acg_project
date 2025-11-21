#include "Material.h"

#include <glm/gtc/constants.hpp>

namespace ACG {

Material::Material()
    : m_baseColor(0.8f, 0.8f, 0.8f)
    , m_metallic(0.0f)
    , m_roughness(0.5f)
    , m_emission(0.0f, 0.0f, 0.0f)
    , m_ior(1.5f)
    , m_opacity(1.0f)
    , m_layerFlags(0)
    , m_extendedDataBaseIndex(0)
    , m_baseColorTexture(nullptr)
    , m_normalTexture(nullptr)
    , m_metallicRoughnessTexture(nullptr)
    , m_emissionTexture(nullptr)
    , m_baseColorTexIdx(-1)
    , m_normalTexIdx(-1)
    , m_metallicRoughnessTexIdx(-1)
    , m_emissionTexIdx(-1)
{
}

Material::~Material() {
}


// Layer management - using vector instead of map (C++17 compatible)
void Material::SetClearcoatLayer(const ClearcoatLayer& layer) {
    m_layerFlags |= LAYER_CLEARCOAT;
    MaterialExtendedData data;
    data.clearcoat = layer;
    m_extendedLayers.push_back(data);
}

const ClearcoatLayer* Material::GetClearcoatLayer() const {
    if (m_layerFlags & LAYER_CLEARCOAT) {
        for (const auto& layer : m_extendedLayers) {
            // Simple heuristic: check if clearcoat flag is set and return first match
            // In practice, Scene will manage layer ordering
            return &layer.clearcoat;
        }
    }
    return nullptr;
}

void Material::SetTransmissionLayer(const TransmissionLayer& layer) {
    m_layerFlags |= LAYER_TRANSMISSION;
    MaterialExtendedData data;
    data.transmission = layer;
    m_extendedLayers.push_back(data);
}

const TransmissionLayer* Material::GetTransmissionLayer() const {
    if (m_layerFlags & LAYER_TRANSMISSION) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.transmission;
        }
    }
    return nullptr;
}

void Material::SetSheenLayer(const SheenLayer& layer) {
    m_layerFlags |= LAYER_SHEEN;
    MaterialExtendedData data;
    data.sheen = layer;
    m_extendedLayers.push_back(data);
}

const SheenLayer* Material::GetSheenLayer() const {
    if (m_layerFlags & LAYER_SHEEN) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.sheen;
        }
    }
    return nullptr;
}

void Material::SetSubsurfaceLayer(const SubsurfaceLayer& layer) {
    m_layerFlags |= LAYER_SUBSURFACE;
    MaterialExtendedData data;
    data.subsurface = layer;
    m_extendedLayers.push_back(data);
}

const SubsurfaceLayer* Material::GetSubsurfaceLayer() const {
    if (m_layerFlags & LAYER_SUBSURFACE) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.subsurface;
        }
    }
    return nullptr;
}

void Material::SetAnisotropyLayer(const AnisotropyLayer& layer) {
    m_layerFlags |= LAYER_ANISOTROPY;
    MaterialExtendedData data;
    data.anisotropy = layer;
    m_extendedLayers.push_back(data);
}

const AnisotropyLayer* Material::GetAnisotropyLayer() const {
    if (m_layerFlags & LAYER_ANISOTROPY) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.anisotropy;
        }
    }
    return nullptr;
}

void Material::SetIridescenceLayer(const IridescenceLayer& layer) {
    m_layerFlags |= LAYER_IRIDESCENCE;
    MaterialExtendedData data;
    data.iridescence = layer;
    m_extendedLayers.push_back(data);
}

const IridescenceLayer* Material::GetIridescenceLayer() const {
    if (m_layerFlags & LAYER_IRIDESCENCE) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.iridescence;
        }
    }
    return nullptr;
}

void Material::SetVolumeLayer(const VolumeLayer& layer) {
    m_layerFlags |= LAYER_VOLUME;
    MaterialExtendedData data;
    data.volume = layer;
    m_extendedLayers.push_back(data);
}

const VolumeLayer* Material::GetVolumeLayer() const {
    if (m_layerFlags & LAYER_VOLUME) {
        for (const auto& layer : m_extendedLayers) {
            return &layer.volume;
        }
    }
    return nullptr;
}

// GPU data conversion
MaterialData Material::ToGPUData() const {
    MaterialData data;
    // Pack into vec4s
    data.baseColor_metallic = glm::vec4(m_baseColor, m_metallic);
    data.emission_roughness = glm::vec4(m_emission, m_roughness);
    
    // Pack ior, opacity, flags, index into vec4 (using memcpy for uint32_t)
    data.ior_opacity_flags_idx.x = m_ior;
    data.ior_opacity_flags_idx.y = m_opacity;
    std::memcpy(&data.ior_opacity_flags_idx.z, &m_layerFlags, sizeof(uint32_t));
    std::memcpy(&data.ior_opacity_flags_idx.w, &m_extendedDataBaseIndex, sizeof(uint32_t));
    
    // Pack texture indices (use memcpy to preserve bit pattern for int interpretation in shader)
    std::memcpy(&data.texIndices.x, &m_baseColorTexIdx, sizeof(int32_t));
    std::memcpy(&data.texIndices.y, &m_normalTexIdx, sizeof(int32_t));
    std::memcpy(&data.texIndices.z, &m_metallicRoughnessTexIdx, sizeof(int32_t));
    std::memcpy(&data.texIndices.w, &m_emissionTexIdx, sizeof(int32_t));
    
    return data;
}

// MTL compatibility methods (convert old format to PBR)
void Material::SetSpecularExponent(float ns) {
    // Convert Phong exponent to roughness
    // ns typically ranges from 0 (rough) to 1000+ (smooth)
    // roughness ranges from 0 (smooth) to 1 (rough)
    m_roughness = std::sqrt(2.0f / (ns + 2.0f));
}

void Material::SetSpecular(const glm::vec3& specular) {
    // Convert specular color to metallic
    // High specular = more metallic-like
    float avg = (specular.r + specular.g + specular.b) / 3.0f;
    m_metallic = glm::clamp(avg, 0.0f, 1.0f);
}

void Material::SetTransmissionFilter(const glm::vec3& tf) {
    // If transmission filter is not white, material is transmissive
    if (glm::length(tf - glm::vec3(1.0f)) > 0.01f) {
        TransmissionLayer layer;
        layer.strength = 1.0f;
        layer.color = tf;
        layer.roughness = m_roughness;
        layer.depth = 1.0f;
        layer.textureIdx = -1;
        SetTransmissionLayer(layer);
    }
}

void Material::SetIllum(int illum) {
    // MTL illumination models:
    // 0: Color on, ambient off
    // 1: Color on, ambient on
    // 2: Highlight on (default)
    // 3: Reflection on, ray trace on
    // 4-10: Various glass/reflection modes
    
    switch (illum) {
        case 0:
        case 1:
        case 2:
            // Standard diffuse/specular
            break;
        case 3:
        case 4:
        case 6:
        case 7:
            // Glass/transmission
            if (!HasLayer(LAYER_TRANSMISSION)) {
                TransmissionLayer layer;
                layer.strength = 0.9f;
                layer.color = glm::vec3(1.0f);
                layer.roughness = m_roughness;
                layer.depth = 1.0f;
                layer.textureIdx = -1;
                SetTransmissionLayer(layer);
            }
            break;
        default:
            break;
    }
}

// Texture setters
void Material::SetBaseColorTexture(std::shared_ptr<Texture> texture, int32_t texIdx) {
    m_baseColorTexture = texture;
    m_baseColorTexIdx = texIdx;
}

void Material::SetNormalTexture(std::shared_ptr<Texture> texture, int32_t texIdx) {
    m_normalTexture = texture;
    m_normalTexIdx = texIdx;
}

void Material::SetMetallicRoughnessTexture(std::shared_ptr<Texture> texture, int32_t texIdx) {
    m_metallicRoughnessTexture = texture;
    m_metallicRoughnessTexIdx = texIdx;
}

void Material::SetEmissionTexture(std::shared_ptr<Texture> texture, int32_t texIdx) {
    m_emissionTexture = texture;
    m_emissionTexIdx = texIdx;
}

} // namespace ACG


