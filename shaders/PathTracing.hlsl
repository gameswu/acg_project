#include "Structures.hlsli"
#include "Random.hlsli"

// Constant buffer
cbuffer RenderParams : register(b0) {
    Camera camera;
    uint frameIndex;
    uint samplesPerPixel;
    uint maxBounces;
    uint numTriangles;
    uint numLights;
    uint numMaterials;
    uint2 resolution;
};

// Structured buffers
StructuredBuffer<Triangle> g_triangles : register(t0);
StructuredBuffer<Material> g_materials : register(t1);
StructuredBuffer<Light> g_lights : register(t2);

// Output texture
RWTexture2D<float4> g_output : register(u0);

// Ray-triangle intersection (Möller-Trumbore)
bool RayTriangleIntersect(Ray ray, Triangle tri, out float t, out float u, out float v) {
    // Initialize out parameters
    t = 1e30;
    u = 0;
    v = 0;
    
    float3 edge1 = tri.v1 - tri.v0;
    float3 edge2 = tri.v2 - tri.v0;
    float3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    
    if (abs(a) < 0.0001) return false;
    
    float f = 1.0 / a;
    float3 s = ray.origin - tri.v0;
    u = f * dot(s, h);
    
    if (u < 0.0 || u > 1.0) return false;
    
    float3 q = cross(s, edge1);
    v = f * dot(ray.direction, q);
    
    if (v < 0.0 || u + v > 1.0) return false;
    
    t = f * dot(edge2, q);
    
    return t > ray.tMin && t < ray.tMax;
}

// Scene intersection (brute force for now)
HitInfo Intersect(Ray ray) {
    HitInfo hit;
    hit.hit = false;
    hit.t = 1e30;
    
    for (uint i = 0; i < numTriangles; i++) {
        Triangle tri = g_triangles[i];
        float t, u, v;
        
        if (RayTriangleIntersect(ray, tri, t, u, v)) {
            if (t < hit.t) {
                hit.hit = true;
                hit.t = t;
                hit.position = ray.origin + ray.direction * t;
                
                // Interpolate normal
                float w = 1.0 - u - v;
                hit.normal = normalize(w * tri.n0 + u * tri.n1 + v * tri.n2);
                hit.texCoord = float2(u, v);
                hit.materialIndex = tri.materialIndex;
            }
        }
    }
    
    return hit;
}

// Evaluate BRDF
float3 EvaluateBRDF(Material mat, float3 wo, float3 wi, float3 normal) {
    float NdotL = max(0.0, dot(normal, wi));
    float NdotV = max(0.0, dot(normal, wo));
    
    if (mat.type == 0) { // Diffuse
        return mat.albedo / 3.14159265359;
    }
    else if (mat.type == 1) { // Specular (Cook-Torrance)
        float3 h = normalize(wo + wi);
        float NdotH = max(0.0, dot(normal, h));
        float VdotH = max(0.0, dot(wo, h));
        
        // GGX NDF
        float alpha = mat.roughness * mat.roughness;
        float alpha2 = alpha * alpha;
        float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
        float D = alpha2 / (3.14159265359 * denom * denom);
        
        // Schlick-GGX G
        float k = (mat.roughness + 1.0) * (mat.roughness + 1.0) / 8.0;
        float G1_V = NdotV / (NdotV * (1.0 - k) + k);
        float G1_L = NdotL / (NdotL * (1.0 - k) + k);
        float G = G1_V * G1_L;
        
        // Fresnel
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), mat.albedo, mat.metallic);
        float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        
        float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
        float3 kD = (1.0 - F) * (1.0 - mat.metallic);
        float3 diffuse = kD * mat.albedo / 3.14159265359;
        
        return diffuse + specular;
    }
    else if (mat.type == 4) { // Emissive
        return mat.emission;
    }
    
    return mat.albedo / 3.14159265359;
}

// Sample BRDF direction
float3 SampleBRDF(Material mat, float3 wo, float3 normal, inout uint rngState, out float pdf) {
    float3 tangent, bitangent;
    CreateCoordinateSystem(normal, tangent, bitangent);
    
    if (mat.type == 0) { // Diffuse
        float2 u = Random2D(rngState);
        float3 localDir = CosineSampleHemisphere(u);
        float3 wi = LocalToWorld(localDir, tangent, bitangent, normal);
        pdf = max(0.0, dot(normal, wi)) / 3.14159265359;
        return wi;
    }
    else if (mat.type == 1) { // Specular (GGX sampling)
        float2 u = Random2D(rngState);
        float alpha = mat.roughness * mat.roughness;
        float alpha2 = alpha * alpha;
        
        float phi = 2.0 * 3.14159265359 * u.x;
        float cosTheta = sqrt((1.0 - u.y) / (1.0 + (alpha2 - 1.0) * u.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        
        float3 h = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        h = LocalToWorld(h, tangent, bitangent, normal);
        
        float3 wi = reflect(-wo, h);
        
        float NdotH = max(0.0, dot(normal, h));
        float VdotH = max(0.0, dot(wo, h));
        float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
        float D = alpha2 / (3.14159265359 * denom * denom);
        pdf = D * NdotH / (4.0 * VdotH);
        
        return wi;
    }
    
    // Default: cosine sampling
    float2 u = Random2D(rngState);
    float3 localDir = CosineSampleHemisphere(u);
    float3 wi = LocalToWorld(localDir, tangent, bitangent, normal);
    pdf = max(0.0, dot(normal, wi)) / 3.14159265359;
    return wi;
}

// Path tracing
float3 TracePath(Ray ray, inout uint rngState) {
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    
    for (uint bounce = 0; bounce < maxBounces; bounce++) {
        HitInfo hit = Intersect(ray);
        
        if (!hit.hit) {
            // Sky color (bright gradient for debugging)
            float t = 0.5 * (ray.direction.y + 1.0);
            radiance += throughput * lerp(float3(1, 1, 1), float3(0.5, 0.7, 1.0), t) * 1.0;
            break;
        }
        
        Material mat = g_materials[hit.materialIndex];
        
        // Add emission (important for area lights!)
        if (mat.type == 4) { // Emissive
            radiance += throughput * mat.emission;
            break; // Stop at light source
        }
        
        // Russian Roulette
        if (bounce > 3) {
            float p = max(throughput.r, max(throughput.g, throughput.b));
            if (Random(rngState) > p) break;
            throughput /= p;
        }
        
        // Sample BRDF
        float pdf;
        float3 wo = -ray.direction;
        float3 wi = SampleBRDF(mat, wo, hit.normal, rngState, pdf);
        
        if (pdf < 0.0001) break;
        
        // Evaluate BRDF
        float3 f = EvaluateBRDF(mat, wo, wi, hit.normal);
        float cosTheta = max(0.0, dot(hit.normal, wi));
        
        throughput *= f * cosTheta / pdf;
        
        // Next ray
        ray.origin = hit.position + hit.normal * 0.001; // Offset to avoid self-intersection
        ray.direction = wi;
        ray.tMin = 0.001;
        ray.tMax = 1e30;
    }
    
    return radiance;
}

// Generate camera ray
Ray GenerateCameraRay(uint2 pixelCoord, inout uint rngState) {
    float2 uv = (float2(pixelCoord) + Random2D(rngState)) / float2(resolution);
    uv = uv * 2.0 - 1.0;
    uv.y = -uv.y;
    
    float aspectRatio = float(resolution.x) / float(resolution.y);
    float halfHeight = tan(camera.fov * 0.5 * 3.14159265359 / 180.0);
    float halfWidth = aspectRatio * halfHeight;
    
    // Construct ray direction in camera space
    float3 rayDir = normalize(camera.direction + uv.x * halfWidth * camera.right + uv.y * halfHeight * camera.up);
    
    Ray ray;
    ray.origin = camera.position;
    ray.direction = rayDir;
    ray.tMin = 0.001;
    ray.tMax = 1e30;
    
    return ray;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    
    if (pixelCoord.x >= resolution.x || pixelCoord.y >= resolution.y) {
        return;
    }
    
    // TEST: Debug intersection with center pixel
    if (pixelCoord.x == resolution.x / 2 && pixelCoord.y == resolution.y / 2) {
        uint rngState = InitRNG(pixelCoord, frameIndex);
        Ray ray = GenerateCameraRay(pixelCoord, rngState);
        
        // Test against first triangle manually
        Triangle tri = g_triangles[0];
        float t, u, v;
        bool hit = RayTriangleIntersect(ray, tri, t, u, v);
        
        // Output debug info to first pixel
        if (hit) {
            g_output[uint2(0, 0)] = float4(1, 0, 0, 1); // RED = HIT triangle 0
        } else {
            g_output[uint2(0, 0)] = float4(0, 1, 0, 1); // GREEN = MISS triangle 0
        }
        
        // Try Intersect function
        HitInfo hitInfo = Intersect(ray);
        if (hitInfo.hit) {
            g_output[uint2(1, 0)] = float4(0, 0, 1, 1); // BLUE = HIT any triangle
        } else {
            g_output[uint2(1, 0)] = float4(1, 1, 0, 1); // YELLOW = MISS all
        }
    }
    
    // Show scene intersection for all pixels
    uint rngState = InitRNG(pixelCoord, frameIndex);
    Ray ray = GenerateCameraRay(pixelCoord, rngState);
    HitInfo hit = Intersect(ray);
    
    if (hit.hit) {
        // Visualize hit distance or material
        float3 color = float3(hit.t / 10.0, 0, 0); // Distance in red channel
        g_output[pixelCoord] = float4(color, 1);
    } else {
        g_output[pixelCoord] = float4(0, 0, 0.1, 1); // Dark blue = miss
    }
    return;
    
    /*
    // Original path tracing code (disabled for testing)
    float3 color = float3(0, 0, 0);
    for (uint i = 0; i < samplesPerPixel; ++i) {
        Ray ray = GenerateCameraRay(pixelCoord, rngState);
    
    float3 color;
    if (hit.hit) {
        // Show hit as green, with brightness based on distance
        float brightness = saturate(1.0 - hit.t / 10.0);
        color = float3(0, brightness, 0);
    } else {
        // Show misses as red
        color = float3(0.1, 0, 0);
    }
    
    g_output[pixelCoord] = float4(color, 1.0);
    return;
    
    /*
    uint rngState = InitRNG(pixelCoord, frameIndex);
    
    // Path tracing
    float3 color = float3(0, 0, 0);
    
    // DEBUG: Simple test - just show ray direction as color
    Ray testRay = GenerateCameraRay(pixelCoord, rngState);
    float3 debugColor = testRay.direction * 0.5 + 0.5; // Map [-1,1] to [0,1]
    
    for (uint s = 0; s < samplesPerPixel; s++) {
        Ray ray = GenerateCameraRay(pixelCoord, rngState);
        color += TracePath(ray, rngState);
    }
    
    color /= float(samplesPerPixel);
    
    // If color is completely black, show debug visualization
    if (length(color) < 0.001) {
        color = debugColor * 0.3; // Dim debug color
    }
    
    // Accumulate with previous frames
    if (frameIndex > 0) {
        float3 prevColor = g_output[pixelCoord].rgb;
        float t = 1.0 / float(frameIndex + 1);
        color = lerp(prevColor, color, t);
    }
    
    g_output[pixelCoord] = float4(color, 1.0);
    */
}
