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
    
    // Get camera basis from cameraToWorld matrix (ROW-MAJOR in HLSL)
    // Matrix is transposed on CPU, so in HLSL:
    // Row 0 = Right axis, Row 1 = Up axis, Row 2 = -Forward axis, Row 3 = Position
    float3 cameraRight = cameraToWorld[0].xyz;     // First row
    float3 cameraUp = cameraToWorld[1].xyz;        // Second row
    float3 cameraForward = -cameraToWorld[2].xyz;  // Third row, camera looks along -Z
    float3 origin = cameraToWorld[3].xyz;          // Fourth row - position
    
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
    
    // DEBUG: Output debug info for center pixel
    if (dispatchIdx.x == renderTargetSize.x / 2 && dispatchIdx.y == renderTargetSize.y / 2 && frameIndex == 0) {
        // This will show in PIX or can be read via debugging
        // For now, just clamp to valid range
    }

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
    // Environment light contribution (controlled by GUI)
    payload.radiance += payload.throughput * environmentLightIntensity;
    payload.terminated = true;
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Get primitive and material
    uint primitiveIndex = PrimitiveIndex();
    uint materialIndex = g_triangleMaterialIndices[primitiveIndex];
    Material mat = g_materials[materialIndex];
    
    // Compute hit point
    float3 rayOrigin = WorldRayOrigin();
    float3 rayDir = WorldRayDirection();
    float t = RayTCurrent();
    float3 hitPos = rayOrigin + t * rayDir;
    
    // Fetch and interpolate normal
    uint i0 = g_indices[primitiveIndex * 3 + 0];
    uint i1 = g_indices[primitiveIndex * 3 + 1];
    uint i2 = g_indices[primitiveIndex * 3 + 2];
    
    float3 n0 = g_vertices[i0].normal;
    float3 n1 = g_vertices[i1].normal;
    float3 n2 = g_vertices[i2].normal;
    
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, 
                                  attribs.barycentrics.x, 
                                  attribs.barycentrics.y);
    float3 normal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    
    // Flip normal if facing away from ray
    if (dot(normal, rayDir) > 0.0) {
        normal = -normal;
    }
    
    // Add emission from this surface
    payload.radiance += payload.throughput * mat.emission.rgb;
    
    // If this is an emissive surface, terminate the path
    float emissionMagnitude = dot(mat.emission.rgb, float3(1, 1, 1));
    if (emissionMagnitude > 0.01) {
        payload.terminated = true;
        return;
    }
    
    // Update throughput: multiply by albedo and divide by PDF
    // For cosine-weighted sampling, PDF = cosTheta / PI
    // BRDF for Lambertian = albedo / PI
    // Combined: (albedo / PI) / (cosTheta / PI) * cosTheta = albedo
    // So we just multiply by albedo (PDF cancels with cosine term)
    payload.throughput *= mat.albedo.rgb;
    
    // Russian roulette path termination (more aggressive for faster convergence)
    float maxThroughput = max(max(payload.throughput.r, payload.throughput.g), payload.throughput.b);
    if (maxThroughput < 0.01) {
        // Terminate very dark paths
        payload.terminated = true;
        return;
    }
    
    // Sample next direction using cosine-weighted hemisphere sampling
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    
    float r1 = Random(payload.rngState);
    float r2 = Random(payload.rngState);
    
    float sinTheta = sqrt(r1);
    float cosTheta_sample = sqrt(1.0 - r1);
    float phi = 2.0 * 3.14159265359 * r2;
    
    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta_sample);
    float3 worldDir = normalize(tangent * localDir.x + bitangent * localDir.y + normal * localDir.z);
    
    // Setup next ray
    payload.nextOrigin = hitPos + normal * 0.001;
    payload.nextDirection = worldDir;
}
