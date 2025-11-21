#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace ACG {

/**
 * @brief Material Layer Flags
 * 
 * Bit flags to indicate which extended layers are active for a material.
 * Uses bit masking for efficient GPU access and flexible layer combinations.
 */
enum MaterialLayerFlags : uint32_t {
    LAYER_NONE           = 0,
    LAYER_CLEARCOAT      = 1 << 0,  // Clear coat layer (car paint, lacquer)
    LAYER_TRANSMISSION   = 1 << 1,  // Transmission/refraction (glass, liquids)
    LAYER_SHEEN          = 1 << 2,  // Sheen layer (fabric, velvet)
    LAYER_SUBSURFACE     = 1 << 3,  // Subsurface scattering (skin, wax, marble)
    LAYER_ANISOTROPY     = 1 << 4,  // Anisotropic reflection (brushed metal)
    LAYER_IRIDESCENCE    = 1 << 5,  // Thin film iridescence (soap bubbles, oil slicks)
    LAYER_VOLUME         = 1 << 6,  // Volume scattering (smoke, fog, clouds)
};

/**
 * @brief Clearcoat Layer
 * 
 * Represents an additional specular layer on top of the base material.
 * Common in automotive paints, lacquered wood, and glossy plastics.
 * 
 * Size: 32 bytes (aligned)
 * Layout for consistent CPU/GPU packing:
 * - Use float4 for vec3+float pairs to avoid padding issues
 */
struct ClearcoatLayer {
    float strength;              // 0-3: Clearcoat intensity [0,1]
    float roughness;             // 4-7: Clearcoat surface roughness [0,1]
    float ior;                   // 8-11: Clearcoat IOR (typically 1.5)
    float padding0;              // 12-15: Padding
    
    glm::vec3 tint;              // 16-27: Clearcoat color tint (usually white)
    int32_t textureIdx;          // 28-31: Clearcoat strength texture (-1 = none)
    
    ClearcoatLayer() 
        : strength(0.0f), roughness(0.0f), ior(1.5f), padding0(0.0f),
          tint(1.0f, 1.0f, 1.0f), textureIdx(-1) {}
};
static_assert(sizeof(ClearcoatLayer) == 32, "ClearcoatLayer must be 32 bytes");

/**
 * @brief Transmission Layer
 * 
 * Enables light transmission through the material (refraction).
 * Used for glass, water, gemstones, and other transparent materials.
 * 
 * Size: 32 bytes (aligned)
 */
struct TransmissionLayer {
    float strength;              // 0-3: Transmission amount [0,1]
    float roughness;             // 4-7: Transmission roughness [0,1]
    float depth;                 // 8-11: Depth scale for absorption
    int32_t textureIdx;          // 12-15: Transmission texture (-1 = none)
    
    glm::vec3 color;             // 16-27: Transmission color filter
    float padding0;              // 28-31: Padding
    
    TransmissionLayer()
        : strength(0.0f), roughness(0.0f), depth(0.0f), textureIdx(-1),
          color(1.0f, 1.0f, 1.0f), padding0(0.0f) {}
};
static_assert(sizeof(TransmissionLayer) == 32, "TransmissionLayer must be 32 bytes");

/**
 * @brief Sheen Layer
 * 
 * Adds a soft fabric-like sheen at grazing angles.
 * Commonly used for cloth, velvet, and other fabric materials.
 * 
 * Size: 32 bytes (aligned)
 */
struct SheenLayer {
    glm::vec3 color;             // 0-11: Sheen color
    float roughness;             // 12-15: Sheen roughness [0,1]
    
    glm::vec3 tint;              // 16-27: Additional color tint
    int32_t textureIdx;          // 28-31: Sheen texture (-1 = none)
    
    SheenLayer()
        : color(0.0f, 0.0f, 0.0f), roughness(0.3f),
          tint(1.0f, 1.0f, 1.0f), textureIdx(-1) {}
};
static_assert(sizeof(SheenLayer) == 32, "SheenLayer must be 32 bytes");

/**
 * @brief Subsurface Scattering Layer
 * 
 * Simulates light scattering beneath the surface.
 * Essential for realistic skin, wax, marble, and translucent materials.
 * 
 * Size: 32 bytes (aligned)
 */
struct SubsurfaceLayer {
    glm::vec3 color;             // 0-11: Subsurface scattering color
    float radius;                // 12-15: Mean free path
    
    glm::vec3 radiusScale;       // 16-27: Per-channel radius scaling
    float anisotropy;            // 28-31: Scattering anisotropy [-1,1]
    
    SubsurfaceLayer()
        : color(1.0f, 0.8f, 0.7f), radius(1.0f), 
          radiusScale(1.0f, 0.5f, 0.3f), anisotropy(0.0f) {}
};
static_assert(sizeof(SubsurfaceLayer) == 32, "SubsurfaceLayer must be 32 bytes");

/**
 * @brief Anisotropy Layer
 * 
 * Enables anisotropic reflections (directional highlights).
 * Used for brushed metal, hair, and other materials with directional structure.
 * 
 * Size: 32 bytes (aligned)
 */
struct AnisotropyLayer {
    float strength;              // 0-3: Anisotropy strength [0,1]
    float rotation;              // 4-7: Rotation [0,1] -> [0°,360°]
    float aspectRatio;           // 8-11: Aspect ratio
    int32_t textureIdx;          // 12-15: Anisotropy texture (-1 = none)
    
    glm::vec3 tangent;           // 16-27: Tangent direction
    float padding0;              // 28-31: Padding
    
    AnisotropyLayer()
        : strength(0.0f), rotation(0.0f), aspectRatio(0.5f), textureIdx(-1),
          tangent(1.0f, 0.0f, 0.0f), padding0(0.0f) {}
};
static_assert(sizeof(AnisotropyLayer) == 32, "AnisotropyLayer must be 32 bytes");

/**
 * @brief Iridescence Layer
 * 
 * Simulates thin-film interference (rainbow-like color shifts).
 * Used for soap bubbles, oil slicks, insect wings, and pearlescent paints.
 * 
 * Size: 32 bytes (aligned)
 */
struct IridescenceLayer {
    float strength;              // 0-3: Iridescence intensity [0,1]
    float ior;                   // 4-7: Thin film IOR (1.3-1.5)
    float thicknessMin;          // 8-11: Min thickness (nm)
    float thicknessMax;          // 12-15: Max thickness (nm)
    
    int32_t textureIdx;          // 16-19: Iridescence texture (-1 = none)
    int32_t thicknessTexIdx;     // 20-23: Thickness map (-1 = none)
    int32_t padding[2];          // 24-31: Padding
    
    IridescenceLayer()
        : strength(0.0f), ior(1.3f), thicknessMin(100.0f), thicknessMax(400.0f),
          textureIdx(-1), thicknessTexIdx(-1), padding{0, 0} {}
};
static_assert(sizeof(IridescenceLayer) == 32, "IridescenceLayer must be 32 bytes");

/**
 * @brief Volume Layer
 * 
 * Enables volumetric scattering and absorption.
 * Used for smoke, fog, clouds, and participating media.
 * 
 * Size: 32 bytes (aligned)
 */
struct VolumeLayer {
    glm::vec3 scatterColor;      // 0-11: Volume scattering color
    float scatterDistance;       // 12-15: Mean free path
    
    glm::vec3 absorptionColor;   // 16-27: Volume absorption color
    float density;               // 28-31: Volume density multiplier
    
    VolumeLayer()
        : scatterColor(1.0f, 1.0f, 1.0f), scatterDistance(1.0f),
          absorptionColor(0.0f, 0.0f, 0.0f), density(1.0f) {}
};
static_assert(sizeof(VolumeLayer) == 32, "VolumeLayer must be 32 bytes");

/**
 * @brief Material Extended Data
 * 
 * Union wrapper for all layer types. Ensures consistent 32-byte size.
 * Used for GPU buffer storage and CPU-GPU data transfer.
 */
struct MaterialExtendedData {
    union {
        ClearcoatLayer clearcoat;
        TransmissionLayer transmission;
        SheenLayer sheen;
        SubsurfaceLayer subsurface;
        AnisotropyLayer anisotropy;
        IridescenceLayer iridescence;
        VolumeLayer volume;
        uint8_t rawData[32];  // Raw byte access
    };
    
    MaterialExtendedData() {
        memset(rawData, 0, 32);
    }
};
static_assert(sizeof(MaterialExtendedData) == 32, "MaterialExtendedData must be 32 bytes");

} // namespace ACG
