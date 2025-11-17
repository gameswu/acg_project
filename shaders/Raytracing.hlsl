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
Buffer<uint> g_indices : register(t1, space1); // Typed buffer for indices
Buffer<uint> g_triangleMaterialIndices : register(t1, space2); // Typed buffer for material indices
StructuredBuffer<Material> g_materials : register(t2);  // Structured buffer for materials
Texture2DArray<float4> g_textures : register(t3);
SamplerState g_sampler : register(s0);

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
    // The matrix columns are the camera's basis vectors in world space
    float3 origin = float3(cameraToWorld[0][3], cameraToWorld[1][3], cameraToWorld[2][3]);
    float3 cameraRight = float3(cameraToWorld[0][0], cameraToWorld[1][0], cameraToWorld[2][0]);
    float3 cameraUp = float3(cameraToWorld[0][1], cameraToWorld[1][1], cameraToWorld[2][1]);
    float3 cameraForward = -float3(cameraToWorld[0][2], cameraToWorld[1][2], cameraToWorld[2][2]); // Camera looks along -Z
    
    // Build ray direction: forward + horizontal offset + vertical offset
    float3 direction = normalize(cameraForward + ndc.x * cameraRight + ndc.y * cameraUp);

    // Path tracing with multiple bounces
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001f;
    ray.TMax = 10000.0f;
    
    // Initialize payload for path tracing
    RadiancePayload payload;
    payload.radiance = float3(0, 0, 0);
    payload.throughput = throughput;
    payload.nextOrigin = float3(0, 0, 0);
    payload.nextDirection = float3(0, 0, 0);
    payload.rngState = rngState;
    payload.terminated = false;
    
    // Iterative path tracing (multiple bounces)
    for (uint bounce = 0; bounce < maxBounces && !payload.terminated; bounce++) {
        // Trace ray
        TraceRay(g_scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        
        // If path terminated, we're done
        if (payload.terminated) {
            break;
        }
        
        // Prepare next ray
        ray.Origin = payload.nextOrigin;
        ray.Direction = payload.nextDirection;
        ray.TMin = 0.001f;
        ray.TMax = 10000.0f;
    }
    
    // Final radiance is accumulated in payload
    radiance = payload.radiance;

    // Progressive accumulation
    // frameIndex is the number of samples already accumulated (0 for first sample)
    if (frameIndex == 0) {
        // First sample: directly write
        g_output[dispatchIdx] = float4(radiance, 1.0);
    } else {
        // Subsequent samples: accumulate with running average
        // Formula: newAverage = (oldAverage * n + newSample) / (n + 1)
        // where n = frameIndex (number of samples already in the average)
        float4 currentValue = g_output[dispatchIdx];
        float n = float(frameIndex);
        float3 newAverage = (currentValue.rgb * n + radiance) / (n + 1.0);
        g_output[dispatchIdx] = float4(newAverage, 1.0);
    }
}

[shader("miss")]
void Miss(inout RadiancePayload payload)
{
    // Add environment light contribution (weighted by throughput)
    payload.radiance += payload.throughput * float3(1, 1, 1) * environmentLightIntensity;
    payload.terminated = true;
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // CRITICAL: PrimitiveIndex() returns the index WITHIN the current geometry
    // GeometryIndex() returns which geometry descriptor was hit
    // We need to compute the global triangle index by accounting for geometry offsets
    
    // Geometry triangle counts (must match C++ mesh order):
    // Mesh 0 (floor): 2 triangles
    // Mesh 1 (ceiling): 2 triangles  
    // Mesh 2 (backWall): 2 triangles
    // Mesh 3 (rightWall): 2 triangles
    // Mesh 4 (leftWall): 2 triangles
    // Mesh 5 (shortBox): 12 triangles
    // Mesh 6 (tallBox): 12 triangles
    // Mesh 7 (light): 2 triangles
    const uint geometryTriangleOffsets[8] = { 0, 2, 4, 6, 8, 10, 22, 34 };
    
    uint geometryIndex = GeometryIndex();
    uint localPrimitiveIndex = PrimitiveIndex();
    uint globalTriangleIndex = geometryTriangleOffsets[geometryIndex] + localPrimitiveIndex;
    
    // Get material
    uint materialIndex = g_triangleMaterialIndices[globalTriangleIndex];
    Material mat = g_materials[materialIndex];
    
    // Get hit point
    float3 rayOrigin = WorldRayOrigin();
    float3 rayDir = WorldRayDirection();
    float t = RayTCurrent();
    float3 hitPos = rayOrigin + t * rayDir;
    
    // Get triangle vertices using indices
    uint i0 = g_indices[globalTriangleIndex * 3 + 0];
    uint i1 = g_indices[globalTriangleIndex * 3 + 1];
    uint i2 = g_indices[globalTriangleIndex * 3 + 2];
    
    // Get vertex normals
    float3 n0 = g_vertices[i0].normal;
    float3 n1 = g_vertices[i1].normal;
    float3 n2 = g_vertices[i2].normal;
    
    // Interpolate normal using barycentric coordinates
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, 
                                  attribs.barycentrics.x, 
                                  attribs.barycentrics.y);
    float3 normal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    
    // Accumulate emission at this surface (weighted by current throughput)
    payload.radiance += payload.throughput * mat.emission.rgb;
    
    // Update throughput for next bounce (multiply by albedo for diffuse)
    payload.throughput *= mat.albedo.rgb;
    
    // Russian roulette termination
    float maxThroughput = max(max(payload.throughput.x, payload.throughput.y), payload.throughput.z);
    if (maxThroughput < 0.01) {
        payload.terminated = true;
        return;
    }
    
    // Sample new direction (cosine-weighted hemisphere)
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    
    float r1 = Random(payload.rngState);
    float r2 = Random(payload.rngState);
    
    float sinTheta = sqrt(r1);
    float cosTheta = sqrt(1.0 - r1);
    float phi = 2.0 * 3.14159265359 * r2;
    
    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    float3 scatterDir = localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
    
    // Set up next ray
    payload.nextOrigin = hitPos + normal * 0.001;
    payload.nextDirection = scatterDir;
}
