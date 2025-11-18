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
    bitangent = normalize(cross(normal, tangent)); // Ensure normalization
}

[shader("raygeneration")]
void RayGen()
{
    uint2 dispatchIdx = DispatchRaysIndex().xy;
    uint2 renderTargetSize = DispatchRaysDimensions().xy;

    // Initialize RNG for this pixel with per-sample variation
    // CRITICAL: Each sample must have different seed to generate different random sequences
    uint rngState = InitRNG(dispatchIdx, frameIndex, frameIndex * 17);
    
    // For pinhole camera, NO pixel jitter - always sample pixel center
    // Variance comes from random bounce directions in path tracing
    float2 pixelCenter = (float2)dispatchIdx + float2(0.5, 0.5);
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

    // Simple additive accumulation - each sample adds its contribution
    // The final division by sample count happens on CPU during readback
    // This avoids read-modify-write race conditions in the shader
    g_output[dispatchIdx] += float4(radiance, 1.0);
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
    
    // Fetch vertex positions and normals
    uint i0 = g_indices[primitiveIndex * 3 + 0];
    uint i1 = g_indices[primitiveIndex * 3 + 1];
    uint i2 = g_indices[primitiveIndex * 3 + 2];
    
    float3 v0 = g_vertices[i0].position;
    float3 v1 = g_vertices[i1].position;
    float3 v2 = g_vertices[i2].position;
    
    float3 n0 = g_vertices[i0].normal;
    float3 n1 = g_vertices[i1].normal;
    float3 n2 = g_vertices[i2].normal;
    
    float2 uv0 = g_vertices[i0].texCoord;
    float2 uv1 = g_vertices[i1].texCoord;
    float2 uv2 = g_vertices[i2].texCoord;
    
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, 
                                  attribs.barycentrics.x, 
                                  attribs.barycentrics.y);
    
    // Interpolate texture coordinates
    float2 texCoord = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
    
    // Calculate true geometric normal from triangle edges
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 faceNormal = normalize(cross(edge1, edge2));
    
    // Interpolated shading normal
    float3 interpolatedNormal = normalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z);
    
    // Ensure face normal points away from the ray (outward from the surface)
    if (dot(faceNormal, rayDir) > 0.0) {
        faceNormal = -faceNormal;
    }
    
    // For simple geometry like Cornell Box, use geometric normal for both offset and shading
    // This avoids issues with inconsistent vertex normals
    float3 geometricNormal = faceNormal;
    float3 normal = faceNormal;  // Use geometric normal for shading to ensure correctness
    
    // Add emission from this surface
    payload.radiance += payload.throughput * mat.emission.rgb;
    
    // If this is an emissive surface, terminate the path
    float emissionMagnitude = dot(mat.emission.rgb, float3(1, 1, 1));
    if (emissionMagnitude > 0.01) {
        payload.terminated = true;
        return;
    }
    
    // ========== MTL ILLUMINATION MODEL CLASSIFICATION ==========
    // Get illum model (authoritative material type identifier)
    int illum = mat.GetIllum();
    uint materialType = mat.GetType();
    
    // MTL Specification: Illumination Models (page 5-7)
    // illum 0: Flat color (no lighting)
    // illum 1: Diffuse (Lambertian)
    // illum 2: Diffuse + Specular (Blinn-Phong) [MOST COMMON]
    // illum 3: Reflection (ray traced mirror)
    // illum 4: Glass (transparency + reflection)
    // illum 5: Fresnel Mirror (perfect reflection)
    // illum 6: Refraction (glass without Fresnel)
    // illum 7: Refraction + Fresnel (realistic glass)
    // illum 8: Reflection (no ray trace)
    // illum 9: Glass (no ray trace)
    // illum 10: Shadow matte
    
    // Handle refractive/transmissive materials
    // Use materialType (set by CPU) rather than illum for accurate classification
    // This handles Blender's illum=4 exports correctly (many are diffuse, not glass)
    if (materialType == 2) {
        // Glass material with reflection and refraction
        float ior = mat.GetIOR();
        float3 N = normal;
        float cosI = -dot(rayDir, N);
        float etaI = 1.0; // Air
        float etaT = ior;  // Material
        
        // Determine if we're entering or exiting the material
        if (cosI < 0.0) {
            // Exiting the material
            cosI = -cosI;
            N = -N;
            float temp = etaI;
            etaI = etaT;
            etaT = temp;
        }
        
        float eta = etaI / etaT;
        float k = 1.0 - eta * eta * (1.0 - cosI * cosI);
        
        // Fresnel reflectance (Schlick's approximation)
        float F0 = ((etaI - etaT) / (etaI + etaT));
        F0 = F0 * F0;
        float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosI, 5.0);
        
        // Russian roulette between reflection and refraction
        float rand = Random(payload.rngState);
        
        if (rand < fresnel || k < 0.0) {
            // Total internal reflection or Fresnel reflection
            float3 reflectDir = reflect(rayDir, N);
            payload.nextOrigin = hitPos + geometricNormal * 0.001;
            payload.nextDirection = reflectDir;
            // Throughput weighted by probability: fresnel / fresnel = 1.0 (no change)
        } else {
            // Refraction
            float3 refractDir = eta * rayDir + (eta * cosI - sqrt(k)) * N;
            payload.nextOrigin = hitPos - geometricNormal * 0.001;
            payload.nextDirection = refractDir;
            payload.throughput *= mat.albedo.rgb;
        }
        return;
    }
    // Handle mirror/reflective materials
    // Use materialType for accurate classification
    else if (materialType == 1) {
        // Perfect mirror reflection using Ks (specular reflectivity)
        float3 reflectDir = reflect(rayDir, normal);
        
        // Use Ks (specular color) as reflectance
        float3 reflectance = mat.specular.rgb;
        // Do not force an artificial high reflectance fallback here; respect Ks provided by material.
        // If Ks is zero, reflection will contribute nothing (correct physical behavior).
        payload.throughput *= reflectance;
        payload.nextOrigin = hitPos + geometricNormal * 0.001;
        payload.nextDirection = reflectDir;
        return;
    }
    // Handle standard diffuse materials (illum 0, 1, 2, default)
    else {
        // Lambertian diffuse BRDF
        // MTL spec: illum 0 = flat color, illum 1 = diffuse, illum 2 = diffuse+specular
        
        // Get base albedo (color or texture)
        float3 albedo = mat.albedo.rgb;
        
            // Sample texture if available (texture overrides base albedo)
            if (mat.HasAlbedoTexture()) {
                int texIndex = mat.GetAlbedoTextureIndex();
                float2 atlasScale = float2(mat.params3.z, mat.params3.w);
                // Only sample if scale is valid (non-zero)
                if (atlasScale.x > 0.0 && atlasScale.y > 0.0) {
                    float2 scaledUV = frac(texCoord) * atlasScale;
                    float4 texColor = g_textures.SampleLevel(g_sampler, float3(scaledUV, texIndex), 0);
                    albedo = texColor.rgb;
                }
            }
        
        // For path tracing with cosine-weighted sampling:
        // BRDF = albedo / PI
        // PDF = cosTheta / PI
        // Combined factor: (albedo / PI) * cosTheta / (cosTheta / PI) = albedo
        payload.throughput *= albedo;
        
        // Russian roulette path termination
        float maxThroughput = max(max(payload.throughput.r, payload.throughput.g), payload.throughput.b);
        if (maxThroughput < 0.001) {
            payload.terminated = true;
            return;
        }
        
        // Sample next direction using cosine-weighted hemisphere sampling
        float3 tangent, bitangent;
        CreateOrthonormalBasis(normal, tangent, bitangent);
        
        float r1 = Random(payload.rngState);
        float r2 = Random(payload.rngState);
        
        float sinTheta = sqrt(r1);
        float cosTheta = sqrt(1.0 - r1);
        float phi = 2.0 * 3.14159265359 * r2;
        
        float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        float3 worldDir = normalize(tangent * localDir.x + bitangent * localDir.y + normal * localDir.z);
        
        payload.nextOrigin = hitPos + geometricNormal * 0.001;
        payload.nextDirection = worldDir;
        return;
    }
}