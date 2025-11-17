#include "Structures.hlsli"
#include "Random.hlsli"

// Payload structure for ray tracing
struct RadiancePayload
{
    float3 radiance;      // Accumulated radiance from emission
    float3 throughput;    // Path throughput
    float3 nextOrigin;    // Next ray origin (for iterative tracing)
    float3 nextDirection; // Next ray direction (for iterative tracing)
    uint rngState;        // RNG state
    bool terminated;      // Path terminated flag
};

// Global root signature
// #define GlobalRootSignature \
//     "DescriptorTable(UAV(u0))," /* Output texture */ \
//     "SRV(t0)," /* Acceleration Structure */ \
//     "SRV(t1)," /* Triangle vertices */ \
//     "SRV(t2)," /* Materials */ \
//     "SRV(t3)," /* Textures */ \
//     "CBV(b0)"  /* Scene constants */

// Scene constants
cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 cameraToWorld;
    float4x4 projectionToCamera;
    uint frameIndex;
    uint maxBounces;
    float environmentLightIntensity;
    float _padding;
}

// Raytracing output
RWTexture2D<float4> g_output : register(u0);

// Acceleration structure
RaytracingAccelerationStructure g_scene : register(t0);

// Geometry data
StructuredBuffer<Vertex> g_vertices : register(t1);
StructuredBuffer<uint> g_indices : register(t1, space1); // Use different space for indices
StructuredBuffer<uint> g_triangleMaterialIndices : register(t1, space2); // Material index per triangle
ByteAddressBuffer g_materialBytes : register(t2);  // Use ByteAddressBuffer instead of StructuredBuffer
Texture2DArray<float4> g_textures : register(t3);
SamplerState g_sampler : register(s0);

// Helper to read material from byte buffer
Material LoadMaterial(uint index) {
    uint baseOffset = index * 96;  // 96 bytes per material
    
    Material mat;
    // Read albedo (offset 0, 16 bytes)
    mat.albedo = asfloat(g_materialBytes.Load4(baseOffset + 0));
    // Read emission (offset 16, 16 bytes)  
    mat.emission = asfloat(g_materialBytes.Load4(baseOffset + 16));
    // Read specular (offset 32, 16 bytes)
    mat.specular = asfloat(g_materialBytes.Load4(baseOffset + 32));
    // Read remaining fields
    mat.type = g_materialBytes.Load(baseOffset + 48);
    mat.metallic = asfloat(g_materialBytes.Load(baseOffset + 52));
    mat.roughness = asfloat(g_materialBytes.Load(baseOffset + 56));
    mat.ior = asfloat(g_materialBytes.Load(baseOffset + 60));
    mat.transmission = asfloat(g_materialBytes.Load(baseOffset + 64));
    mat.albedoTextureIndex = asint(g_materialBytes.Load(baseOffset + 68));
    mat.illum = asint(g_materialBytes.Load(baseOffset + 72));
    mat.albedoTextureSize = asfloat(g_materialBytes.Load2(baseOffset + 76));
    // padding at 84-95 is ignored
    
    return mat;
}

// Helper function to create orthonormal basis from normal
void CreateOrthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    // Choose a helper vector that's not parallel to normal
    float3 helper = abs(normal.x) > 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    tangent = normalize(cross(normal, helper));
    bitangent = cross(normal, tangent);
}

[shader("raygeneration")]
void RayGen()
{
    uint2 dispatchIdx = DispatchRaysIndex().xy;
    uint2 renderTargetSize = DispatchRaysDimensions().xy;

    // Initialize RNG for this pixel
    uint rngState = InitRNG(dispatchIdx, frameIndex);
    
    // Generate ray from camera with slight jitter for anti-aliasing
    float2 pixelCenter = (float2)dispatchIdx + float2(0.5, 0.5); // No jitter for now
    float2 uv = pixelCenter / (float2)renderTargetSize; // [0,1]
    
    // Simple pinhole camera model
    // Assume FOV of 60 degrees
    float aspectRatio = (float)renderTargetSize.x / (float)renderTargetSize.y;
    float fov = 60.0 * 3.14159265 / 180.0; // 60 degrees in radians
    float tanHalfFov = tan(fov * 0.5);
    
    // NDC coordinates ([-1,1] range)
    float2 ndc = uv * 2.0 - 1.0;
    ndc.x *= aspectRatio * tanHalfFov;
    ndc.y *= -tanHalfFov; // Flip Y
    
    // Get camera basis from cameraToWorld matrix
    // After transpose, the matrix layout in HLSL row-major is:
    // [Xx  Yx  Zx  Tx]
    // [Xy  Yy  Zy  Ty]
    // [Xz  Yz  Zz  Tz]
    // [0   0   0   1 ]
    // So we need to read columns to get the basis vectors
    float3 origin = float3(cameraToWorld[0][3], cameraToWorld[1][3], cameraToWorld[2][3]);
    float3 cameraRight = float3(cameraToWorld[0][0], cameraToWorld[1][0], cameraToWorld[2][0]);
    float3 cameraUp = float3(cameraToWorld[0][1], cameraToWorld[1][1], cameraToWorld[2][1]);
    float3 cameraZ = float3(cameraToWorld[0][2], cameraToWorld[1][2], cameraToWorld[2][2]);
    
    // Build ray direction (camera looks along -Z, so we negate cameraZ)
    float3 direction = normalize(-cameraZ + ndc.x * cameraRight + ndc.y * cameraUp);

    // Simple single-bounce path tracing (no loop to avoid GPU timeout)
    float3 radiance = float3(0, 0, 0);
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;
    
    // Initialize payload for primary ray
    RadiancePayload payload;
    payload.radiance = float3(0, 0, 0);
    payload.throughput = float3(1, 1, 1);
    payload.nextOrigin = float3(0, 0, 0);
    payload.nextDirection = float3(0, 0, 0);
    payload.rngState = rngState;
    payload.terminated = false;
    
    // Trace primary ray only (single bounce for now)
    TraceRay(g_scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    radiance = payload.radiance;

    // Progressive accumulation
    float4 currentValue = g_output[dispatchIdx];
    
    if (frameIndex == 0) {
        g_output[dispatchIdx] = float4(radiance, 1.0);
    } else {
        float n = float(frameIndex);
        float3 accumulated = currentValue.rgb * n + radiance;
        float3 newAverage = accumulated / (n + 1.0);
        g_output[dispatchIdx] = float4(newAverage, 1.0);
    }
}

[shader("miss")]
void Miss(inout RadiancePayload payload)
{
    // Use environment light
    payload.radiance = float3(1, 1, 1) * environmentLightIntensity;
    payload.terminated = true;
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Get material
    uint primitiveIndex = PrimitiveIndex();
    uint materialIndex = g_triangleMaterialIndices[primitiveIndex];
    
    // Clamp material index to valid range
    materialIndex = min(materialIndex, 8u);
    
    // Load material using byte buffer
    Material mat = LoadMaterial(materialIndex);
    
    // Show emission if exists, otherwise show albedo with ambient
    if (length(mat.emission.rgb) > 0.01) {
        payload.radiance = mat.emission.rgb;
    } else {
        payload.radiance = mat.albedo.rgb * (0.3 + environmentLightIntensity * 0.7);
    }
    
    payload.terminated = true;
}
