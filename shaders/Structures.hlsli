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
    // CRITICAL: Must match C++ glm::vec4 layout (each vec4 = 16 bytes)
    // C++ side uses vec4, so we must use float4 here to match padding
    
    // vec4 #1: baseColor_metallic (16 bytes)
    float4 baseColor_metallic;  // 0-15: RGB diffuse/albedo + metallic
    
    // vec4 #2: emission_roughness (16 bytes)
    float4 emission_roughness;  // 16-31: RGB emission (HDR) + roughness
    
    // vec4 #3: ior_opacity_flags_idx (16 bytes)
    float4 ior_opacity_flags_idx; // 32-47: X=IOR, Y=opacity, Z=layerFlags(as float), W=extendedDataIndex(as float)
    
    // vec4 #4: texIndices (16 bytes) - packed layout
    float4 texIndices;          // 48-63: X=baseColorTex(int32), Y=normalTex(int32), Z=packed(mr:16|emission:16), W=packed(opacity:16|pad:16)
    
    // Total: 64 bytes (4 x 16-byte vec4s)
    
    // Helper accessors to extract individual fields
    float3 baseColor() { return baseColor_metallic.xyz; }
    float metallic() { return baseColor_metallic.w; }
    float3 emission() { return emission_roughness.xyz; }
    float roughness() { return emission_roughness.w; }
    float ior() { return ior_opacity_flags_idx.x; }
    float opacity() { return ior_opacity_flags_idx.y; }
    uint layerFlags() { return asuint(ior_opacity_flags_idx.z); }
    uint extendedDataIndex() { return asuint(ior_opacity_flags_idx.w); }
    
    // Texture index helpers - with 16-bit packing for indices Z and W
    int baseColorTexIdx() { return asint(texIndices.x); }
    int normalTexIdx() { return asint(texIndices.y); }
    int metallicRoughnessTexIdx() { uint packed = asuint(texIndices.z); return int(packed & 0xFFFF) - 32768; }  // Extract low 16 bits, convert from uint16 to int16
    int emissionTexIdx() { uint packed = asuint(texIndices.z); return int(packed >> 16) - 32768; }  // Extract high 16 bits, convert from uint16 to int16
    int opacityTexIdx() { uint packed = asuint(texIndices.w); return int(packed & 0xFFFF) - 32768; }  // Extract low 16 bits, convert from uint16 to int16
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
    float strength;             // 28-31: Subsurface weight
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

// ============================================================================
// Material Layer Access Helpers (requires g_materialLayers buffer)
// ============================================================================

// Load VolumeLayer from extended data buffer
VolumeLayer LoadVolumeLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    VolumeLayer vol;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    // Decode from float4x2 storage (32 bytes total)
    vol.scatterColor = data.data0.xyz;       // 0-11: scatter color
    vol.scatterDistance = data.data0.w;      // 12-15: scatter distance
    vol.absorptionColor = data.data1.xyz;    // 16-27: absorption color
    vol.density = data.data1.w;              // 28-31: density
    
    return vol;
}

// Load TransmissionLayer from extended data buffer
TransmissionLayer LoadTransmissionLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    TransmissionLayer trans;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    trans.strength = data.data0.x;           // 0-3: strength
    trans.roughness = data.data0.y;          // 4-7: roughness
    trans.depth = data.data0.z;              // 8-11: depth
    trans.textureIdx = asint(data.data0.w);  // 12-15: texture index
    trans.color = data.data1.xyz;            // 16-27: color
    
    return trans;
}

// Load ClearcoatLayer from extended data buffer
ClearcoatLayer LoadClearcoatLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    ClearcoatLayer coat;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    coat.strength = data.data0.x;            // 0-3: strength
    coat.roughness = data.data0.y;           // 4-7: roughness
    coat.ior = data.data0.z;                 // 8-11: IOR
    coat.tint = data.data1.xyz;              // 16-27: tint
    coat.textureIdx = asint(data.data1.w);   // 28-31: texture index
    
    return coat;
}

// Load SheenLayer from extended data buffer
SheenLayer LoadSheenLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    SheenLayer sheen;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    sheen.color = data.data0.xyz;            // 0-11: color
    sheen.roughness = data.data0.w;          // 12-15: roughness
    sheen.tint = data.data1.xyz;             // 16-27: tint
    sheen.textureIdx = asint(data.data1.w);  // 28-31: texture index
    
    return sheen;
}

// Load SubsurfaceLayer from extended data buffer
SubsurfaceLayer LoadSubsurfaceLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    SubsurfaceLayer sss;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    sss.color = data.data0.xyz;              // 0-11: color
    sss.radius = data.data0.w;               // 12-15: radius
    sss.radiusScale = data.data1.xyz;        // 16-27: radius scale (per channel)
    sss.strength = data.data1.w;             // 28-31: strength (subsurface weight)
    
    return sss;
}

// Load AnisotropyLayer from extended data buffer
AnisotropyLayer LoadAnisotropyLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    AnisotropyLayer aniso;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    aniso.strength = data.data0.x;           // 0-3: strength
    aniso.rotation = data.data0.y;           // 4-7: rotation (radians)
    aniso.aspectRatio = data.data0.z;        // 8-11: aspect ratio
    aniso.textureIdx = asint(data.data0.w);  // 12-15: texture index
    aniso.tangent = data.data1.xyz;          // 16-27: tangent direction
    
    return aniso;
}

// Load IridescenceLayer from extended data buffer
IridescenceLayer LoadIridescenceLayer(uint extendedDataIndex, StructuredBuffer<MaterialExtendedData> layerBuffer)
{
    IridescenceLayer irid;
    MaterialExtendedData data = layerBuffer[extendedDataIndex];
    
    irid.strength = data.data0.x;                // 0-3: strength
    irid.ior = data.data0.y;                     // 4-7: IOR of thin film
    irid.thicknessMin = data.data0.z;            // 8-11: min thickness (nm)
    irid.thicknessMax = data.data0.w;            // 12-15: max thickness (nm)
    irid.textureIdx = asint(data.data1.x);       // 16-19: texture index
    irid.thicknessTexIdx = asint(data.data1.y);  // 20-23: thickness texture index
    
    return irid;
}

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
