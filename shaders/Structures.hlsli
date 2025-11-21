// Shared structures between CPU and GPU
#ifndef STRUCTURES_HLSLI
#define STRUCTURES_HLSLI

struct Vertex {
    float3 position;  // 12 bytes
    float3 normal;    // 12 bytes
    float2 texCoord;  // 8 bytes
    float3 tangent;   // 12 bytes
    float _pad;       // 4 bytes padding -> total 48 bytes (aligned to 16)
};

// ============================================================================
// Material Layer Flags (must match MaterialLayers.h)
// ============================================================================
#define LAYER_NONE           0
#define LAYER_CLEARCOAT      (1 << 0)
#define LAYER_TRANSMISSION   (1 << 1)
#define LAYER_SHEEN          (1 << 2)
#define LAYER_SUBSURFACE     (1 << 3)
#define LAYER_ANISOTROPY     (1 << 4)
#define LAYER_IRIDESCENCE    (1 << 5)
#define LAYER_VOLUME         (1 << 6)

// ============================================================================
// Material Structure (64 bytes, matches MaterialData in Material.h)
// ============================================================================
struct Material {
    // Base PBR properties (32 bytes)
    float3 baseColor;           // 0-11: RGB diffuse/albedo
    float metallic;             // 12-15: Metallic factor [0,1]
    
    float3 emission;            // 16-27: Emissive color (HDR)
    float roughness;            // 28-31: Surface roughness [0,1]
    
    float ior;                  // 32-35: Index of refraction
    float opacity;              // 36-39: Opacity [0,1]
    uint layerFlags;            // 40-43: Bit flags for enabled layers
    uint extendedDataIndex;     // 44-47: Index into layer buffer
    
    // Texture indices (16 bytes)
    int baseColorTexIdx;        // 48-51: Base color texture (-1 = none)
    int normalTexIdx;           // 52-55: Normal map (-1 = none)
    int metallicRoughnessTexIdx;// 56-59: Packed texture (-1 = none)
    int emissionTexIdx;         // 60-63: Emission texture (-1 = none)
    
    // Total: 64 bytes
};

// ============================================================================
// Extended Layer Structures (32 bytes each, matches MaterialLayers.h)
// ============================================================================

struct ClearcoatLayer {
    float strength;             // 0-3
    float roughness;            // 4-7
    float ior;                  // 8-11
    float padding0;             // 12-15
    
    float3 tint;                // 16-27
    int textureIdx;             // 28-31
};

struct TransmissionLayer {
    float strength;             // 0-3
    float roughness;            // 4-7
    float depth;                // 8-11
    int textureIdx;             // 12-15
    
    float3 color;               // 16-27
    float padding0;             // 28-31
};

struct SheenLayer {
    float3 color;               // 0-11
    float roughness;            // 12-15
    
    float3 tint;                // 16-27
    int textureIdx;             // 28-31
};

struct SubsurfaceLayer {
    float3 color;               // 0-11
    float radius;               // 12-15
    
    float3 radiusScale;         // 16-27
    float anisotropy;           // 28-31
};

struct AnisotropyLayer {
    float strength;             // 0-3
    float rotation;             // 4-7
    float aspectRatio;          // 8-11
    int textureIdx;             // 12-15
    
    float3 tangent;             // 16-27
    float padding0;             // 28-31
};

struct IridescenceLayer {
    float strength;             // 0-3
    float ior;                  // 4-7
    float thicknessMin;         // 8-11
    float thicknessMax;         // 12-15
    
    int textureIdx;             // 16-19
    int thicknessTexIdx;        // 20-23
    int2 padding;               // 24-31
};

struct VolumeLayer {
    float3 scatterColor;        // 0-11
    float scatterDistance;      // 12-15
    
    float3 absorptionColor;     // 16-27
    float density;              // 28-31
};

// ============================================================================
// Material Extended Data (Union replacement for HLSL)
// ============================================================================
// In C++ this is a union of all layer types. In HLSL we use raw float4x2 storage.
// Total: 32 bytes = 8 floats = 2 float4s
struct MaterialExtendedData {
    float4 data0;  // Bytes 0-15
    float4 data1;  // Bytes 16-31
};

struct Light {
    uint type;          // 0=Point, 1=Directional, 2=Area, 3=Environment
    float3 color;
    float intensity;
    float3 position;
    float3 direction;
    float2 size;
    uint padding[2];
};

struct BVHNode {
    float3 bboxMin; float _pad0;
    float3 bboxMax; float _pad1;
    int leftChild;      // -1 if leaf
    int rightChild;     // -1 if leaf
    int firstPrim;
    int primCount;
};

struct Triangle {
    float3 v0; float _pad_v0;
    float3 v1; float _pad_v1;
    float3 v2; float _pad_v2;
    float3 n0; float _pad_n0;
    float3 n1; float _pad_n1;
    float3 n2; float _pad_n2;
    float2 t0; float2 _pad_t0;
    float2 t1; float2 _pad_t1;
    float2 t2; float2 _pad_t2;
    uint materialIndex;
    uint _pad_mat[3];
};

struct Ray {
    float3 origin;
    float3 direction;
    float tMin;
    float tMax;
};

struct HitInfo {
    bool hit;
    float t;
    float3 position;
    float3 normal;
    float2 texCoord;
    uint materialIndex;
};

struct Camera {
    float3 position;
    float _pad0;
    float3 direction;
    float _pad1;
    float3 right;
    float _pad2;
    float3 up;
    float _pad3;
    float fov;
    float aspectRatio;
    float aperture;
    float focusDistance;
};

#endif // STRUCTURES_HLSLI
