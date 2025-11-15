#include "Structures.hlsli"
#include "Random.hlsli"

cbuffer RenderParams : register(b0) {
    Camera camera;              // 80 bytes (5 float4)
    uint frameIndex;            // 4
    uint samplesPerPixel;       // 4
    uint maxBounces;            // 4
    uint numTriangles;          // 4
    uint numMaterials;          // 4
    uint _pad0;                 // 4
    uint _pad1;                 // 4
    uint _pad2;                 // 4
    uint resolutionX;           // 4
    uint resolutionY;           // 4
    uint2 _pad3;                // 8 (for 16-byte alignment)
};

StructuredBuffer<Triangle> g_triangles : register(t0);
StructuredBuffer<Material> g_materials : register(t1);
RWTexture2D<float4> g_output : register(u0);

bool RayTriangleIntersect(Ray ray, Triangle tri, out float t, out float u, out float v) {
    t = 1e30;
    u = 0;
    v = 0;
    
    float3 v0 = tri.v0;
    float3 v1 = tri.v1;
    float3 v2 = tri.v2;
    
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    
    if (abs(a) < 0.0001) return false;
    
    float f = 1.0 / a;
    float3 s = ray.origin - v0;
    u = f * dot(s, h);
    
    if (u < 0.0 || u > 1.0) return false;
    
    float3 q = cross(s, edge1);
    v = f * dot(ray.direction, q);
    
    if (v < 0.0 || u + v > 1.0) return false;
    
    t = f * dot(edge2, q);
    
    return t > ray.tMin && t < ray.tMax;
}

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
                
                float w = 1.0 - u - v;
                float3 n0 = tri.n0;
                float3 n1 = tri.n1;
                float3 n2 = tri.n2;
                hit.normal = normalize(w * n0 + u * n1 + v * n2);
                hit.texCoord = float2(u, v);
                hit.materialIndex = tri.materialIndex;
            }
        }
    }
    
    return hit;
}

float3 EvaluateBRDF(Material mat, float3 wo, float3 wi, float3 normal) {
    float NdotL = max(0.0, dot(normal, wi));
    float3 albedo = float3(mat.albedo[0], mat.albedo[1], mat.albedo[2]);
    
    if (mat.type == 0) {
        // Diffuse BRDF
        return albedo / 3.14159265359;
    } else if (mat.type == 1) {
        // Specular (Mirror) - delta function, only used for direct hits
        return float3(0, 0, 0);
    }
    
    // Default diffuse
    return albedo / 3.14159265359;
}

float3 SampleBRDF(Material mat, float3 wo, float3 normal, inout uint rngState, out float pdf) {
    if (mat.type == 1) {
        // Specular (Perfect mirror reflection)
        pdf = 1.0;
        return reflect(-wo, normal);
    }
    
    // Diffuse sampling
    float3 tangent, bitangent;
    CreateCoordinateSystem(normal, tangent, bitangent);
    
    float2 u = Random2D(rngState);
    float3 localDir = CosineSampleHemisphere(u);
    float3 wi = LocalToWorld(localDir, tangent, bitangent, normal);
    pdf = max(0.0001, dot(normal, wi) / 3.14159265359);
    
    return wi;
}

float3 TracePath(Ray ray, inout uint rngState) {
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    
    for (uint bounce = 0; bounce < maxBounces; bounce++) {
        HitInfo hit = Intersect(ray);
        
        if (!hit.hit) {
            // Cornell Box is closed - no environment light
            break;
        }
        
        Material mat = g_materials[hit.materialIndex];
        
        // Direct light contribution from hit emissive surface
        if (mat.type == 4) {
            float3 emission = float3(mat.emission[0], mat.emission[1], mat.emission[2]);
            radiance += throughput * emission;
            break;
        }
        
        // Next Event Estimation: sample light source directly
        if (bounce == 0 || bounce == 1) {
            // Find emissive material and sample it
            for (uint i = 0; i < numTriangles; i++) {
                Triangle lightTri = g_triangles[i];
                if (lightTri.materialIndex == 8) { // Light material index
                    // Sample random point on light triangle
                    float r1 = Random(rngState);
                    float r2 = Random(rngState);
                    float sqrtR1 = sqrt(r1);
                    float u = 1.0 - sqrtR1;
                    float v = r2 * sqrtR1;
                    float w = 1.0 - u - v;
                    
                    float3 lv0 = lightTri.v0;
                    float3 lv1 = lightTri.v1;
                    float3 lv2 = lightTri.v2;
                    float3 lightPos = w * lv0 + u * lv1 + v * lv2;
                    
                    // Compute light normal
                    float3 ln0 = lightTri.n0;
                    float3 ln1 = lightTri.n1;
                    float3 ln2 = lightTri.n2;
                    float3 lightNormal = normalize(w * ln0 + u * ln1 + v * ln2);
                    
                    // Direction to light
                    float3 toLight = lightPos - hit.position;
                    float distToLight = length(toLight);
                    float3 lightDir = toLight / distToLight;
                    
                    // Check if light is above surface
                    float NdotL = dot(hit.normal, lightDir);
                    float LdotN = dot(lightNormal, -lightDir);
                    
                    if (NdotL > 0.0 && LdotN > 0.0) {
                        // Shadow ray
                        Ray shadowRay;
                        shadowRay.origin = hit.position + hit.normal * 0.001;
                        shadowRay.direction = lightDir;
                        shadowRay.tMin = 0.001;
                        shadowRay.tMax = distToLight - 0.001;
                        
                        HitInfo shadowHit = Intersect(shadowRay);
                        
                        if (!shadowHit.hit) {
                            // Light visible
                            Material lightMat = g_materials[8];
                            float3 lightEmission = float3(lightMat.emission[0], lightMat.emission[1], lightMat.emission[2]);
                            
                            // Compute light area (approximate)
                            float3 edge1 = lv1 - lv0;
                            float3 edge2 = lv2 - lv0;
                            float lightArea = length(cross(edge1, edge2)) * 0.5;
                            
                            // BRDF
                            float3 f = EvaluateBRDF(mat, -ray.direction, lightDir, hit.normal);
                            
                            // Geometry term
                            float G = (NdotL * LdotN) / (distToLight * distToLight);
                            
                            // Direct lighting contribution (scaled by triangle count as we only sample one)
                            radiance += throughput * f * lightEmission * G * lightArea * 2.0;
                        }
                    }
                    break; // Only sample first light triangle
                }
            }
        }
        
        // Russian Roulette
        if (bounce > 3) {
            float p = max(throughput.r, max(throughput.g, throughput.b));
            if (Random(rngState) > p) break;
            throughput /= p;
        }
        
        float pdf;
        float3 wo = -ray.direction;
        float3 wi = SampleBRDF(mat, wo, hit.normal, rngState, pdf);
        
        if (pdf < 0.0001) break;
        
        // For specular materials, use direct throughput multiplication
        if (mat.type == 1) {
            // Perfect specular reflection
            float3 albedo = float3(mat.albedo[0], mat.albedo[1], mat.albedo[2]);
            throughput *= albedo;
        } else {
            // Diffuse and other materials
            float3 f = EvaluateBRDF(mat, wo, wi, hit.normal);
            float cosTheta = max(0.0, dot(hit.normal, wi));
            throughput *= f * cosTheta / pdf;
        }
        
        // Clamp throughput to avoid fireflies
        throughput = min(throughput, float3(10.0, 10.0, 10.0));
        
        // Check for invalid values (NaN/Inf)
        if (any(throughput != throughput) || any(throughput > 1e10)) {
            break;
        }
        
        ray.origin = hit.position + hit.normal * 0.001;
        ray.direction = wi;
        ray.tMin = 0.001;
        ray.tMax = 1e30;
    }
    
    return radiance;
}

Ray GenerateCameraRay(uint2 pixelCoord, inout uint rngState) {
    float2 resolution = float2(float(resolutionX), float(resolutionY));
    float2 uv = (float2(pixelCoord) + float2(0.5, 0.5)) / resolution;
    uv = uv * 2.0 - 1.0;
    
    float aspectRatio = resolution.x / resolution.y;
    float halfHeight = tan(camera.fov * 0.5 * 3.14159265359 / 180.0);
    float halfWidth = aspectRatio * halfHeight;
    
    float3 rayDir = normalize(
        camera.direction + 
        uv.x * halfWidth * camera.right + 
        uv.y * halfHeight * camera.up
    );
    
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
    
    if (pixelCoord.x >= resolutionX || pixelCoord.y >= resolutionY) {
        return;
    }
    
    uint rngState = InitRNG(pixelCoord, frameIndex);
    
    float3 finalColor = float3(0, 0, 0);
    for (uint i = 0; i < samplesPerPixel; ++i) {
        Ray ray = GenerateCameraRay(pixelCoord, rngState);
        finalColor += TracePath(ray, rngState);
    }
    
    finalColor /= float(samplesPerPixel);
    
    // Tone mapping and gamma correction
    finalColor = finalColor / (finalColor + 1.0);
    finalColor = pow(abs(finalColor), 1.0 / 2.2);
    
    g_output[pixelCoord] = float4(finalColor, 1.0);
}
