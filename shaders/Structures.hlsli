// Shared structures between CPU and GPU
#ifndef STRUCTURES_HLSLI
#define STRUCTURES_HLSLI

struct Vertex {
    float3 position;
    float3 normal;
    float2 texCoord;
    float3 tangent;
};

struct Material {
    uint type;          // 0=Diffuse, 1=Specular, 2=Transmissive, 3=PrincipledBSDF, 4=Emissive
    float albedo[3];
    float emission[3];
    float metallic;
    float roughness;
    float ior;
    float transmission;
    uint padding[1];
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
