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

// Simplified Material structure - only float4 fields to avoid alignment issues
// This matches C++ GPUMaterial but forces 16-byte alignment for all fields
struct Material {
    float4 albedo;              // 0-15: 16 bytes
    float4 emission;            // 16-31: 16 bytes
    float4 specular;            // 32-47: 16 bytes
    float4 params1;             // 48-63: type, metallic, roughness, ior
    float4 params2;             // 64-79: transmission, albedoTextureIndex, illum, (unused)
    float4 params3;             // 80-95: albedoTextureSize.xy, padding.xy
    // Total: 96 bytes (6 * 16) - guaranteed aligned
    
    // Helper accessors
    uint GetType() { return asuint(params1.x); }
    float GetMetallic() { return params1.y; }
    float GetRoughness() { return params1.z; }
    float GetIOR() { return params1.w; }
    float GetTransmission() { return params2.x; }
    int GetAlbedoTextureIndex() { return asint(params2.y); }
    int GetIllum() { return asint(params2.z); }
    float2 GetAlbedoTextureSize() { return params3.xy; }
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
