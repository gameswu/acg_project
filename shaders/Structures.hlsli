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

// Material structure designed for GPU path tracing
// Layout follows Wavefront MTL specification (v4.2, October 1995)
// All fields are float4 (16-byte aligned) for consistent memory access
//
// MTL Material Properties Mapping:
// - albedo.rgb  = Kd (Diffuse reflectivity, 0.0-1.0)
// - albedo.a    = d (Dissolve factor, 1.0=opaque)
// - emission.rgb = Ke (Emissive color, 0.0+)
// - specular.rgb = Ks (Specular reflectivity, 0.0-1.0)
// - specular.a  = Ns (Specular exponent, 0-1000)
// - params1.w   = Ni (Optical density/IOR, 0.001-10)
// - params2.z   = illum (Illumination model, 0-10)
//
// Illumination Models (MTL spec):
// 0  = Flat color (no lighting)
// 1  = Diffuse (Lambertian)
// 2  = Diffuse + Specular (Blinn-Phong) [most common]
// 3  = Reflection (ray traced)
// 4  = Glass (transparency + reflection)
// 5  = Fresnel Mirror (perfect reflection)
// 6  = Refraction (Fresnel off)
// 7  = Refraction + Fresnel (realistic glass)
// 8  = Reflection (no ray trace)
// 9  = Glass (no ray trace)
// 10 = Shadow matte
struct Material {
    float4 albedo;              // 0-15: Kd (RGB) + d (A)
    float4 emission;            // 16-31: Ke (RGB) + intensity (A)
    float4 specular;            // 32-47: Ks (RGB) + Ns (A)
    float4 params1;             // 48-63: type, metallic, roughness, Ni
    float4 params2;             // 64-79: transmission, albedoTextureIndex, illum, subsurface
    float4 params3;             // 80-95: textureWidth, textureHeight, textureScaleX, textureScaleY
    // Total: 96 bytes (6 * 16) - guaranteed aligned
    
    // Helper accessors for type-safe parameter access
    uint GetType() { return uint(round(params1.x)); }
    float GetMetallic() { return params1.y; }
    float GetRoughness() { return params1.z; }
    float GetIOR() { return params1.w; }
    float GetTransmission() { return params2.x; }
    int GetAlbedoTextureIndex() { return int(round(params2.y)); }
    int GetIllum() { return int(round(params2.z)); }
    float GetSubsurface() { return params2.w; }
    float2 GetTextureSize() { return params3.xy; }
    float2 GetTextureScale() { return params3.zw; }
    
    // Texture sampling helpers (for future use)
    bool HasAlbedoTexture() { return GetAlbedoTextureIndex() >= 0; }
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
