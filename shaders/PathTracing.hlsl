#include "Structures.hlsli"
#include "Random.hlsli"

cbuffer RenderParams : register(b0) {
    Camera camera;              // 80 bytes (5 float4)
    uint frameIndex;            // 4
    uint samplesPerPixel;       // 4
    uint maxBounces;            // 4
    uint numTriangles;          // 4
    uint numMaterials;          // 4
    uint accumulatedSamples;    // 4 - global sample offset
    float environmentLight;     // 4 - environment light intensity
    uint _pad2;                 // 4
    uint resolutionX;           // 4
    uint resolutionY;           // 4
    uint2 _pad3;                // 8 (for 16-byte alignment)
};

StructuredBuffer<Triangle> g_triangles : register(t0);
StructuredBuffer<Material> g_materials : register(t1);
Texture2D<float4> g_accumulation : register(t2);  // Previous accumulated samples
StructuredBuffer<BVHNode> g_bvhNodes : register(t3);  // BVH acceleration structure
StructuredBuffer<int> g_bvhTriangleIndices : register(t4);  // Triangle index remapping for BVH
RWTexture2D<float4> g_output : register(u0);      // Current frame output
RWTexture2D<float4> g_accum_out : register(u1);   // Updated accumulation

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

// AABB-Ray intersection test
bool IntersectAABB(float3 bboxMin, float3 bboxMax, Ray ray, float tMax) {
    float3 invDir = 1.0 / ray.direction;
    
    // Handle near-zero direction components to avoid inf/nan
    if (abs(ray.direction.x) < 1e-8) invDir.x = 1e8 * sign(ray.direction.x);
    if (abs(ray.direction.y) < 1e-8) invDir.y = 1e8 * sign(ray.direction.y);
    if (abs(ray.direction.z) < 1e-8) invDir.z = 1e8 * sign(ray.direction.z);
    
    float3 t0 = (bboxMin - ray.origin) * invDir;
    float3 t1 = (bboxMax - ray.origin) * invDir;
    
    float3 tSmaller = min(t0, t1);
    float3 tBigger = max(t0, t1);
    
    float tMin = max(max(tSmaller.x, tSmaller.y), max(tSmaller.z, ray.tMin));
    float tMaxAABB = min(min(tBigger.x, tBigger.y), min(tBigger.z, tMax));
    
    return tMin <= tMaxAABB && tMaxAABB > 0.0;
}

HitInfo Intersect(Ray ray) {
    HitInfo hit;
    hit.hit = false;
    hit.t = 1e30;
    
    // Check if scene is loaded
    if (numTriangles == 0) {
        return hit;
    }

    // BVH traversal
    int stack[32];
    int stackPtr = 0;
    stack[stackPtr++] = 0; // Start from root node
    
    while (stackPtr > 0) {
        
        int nodeIdx = stack[--stackPtr];
        
        BVHNode node = g_bvhNodes[nodeIdx];
        
        // Test AABB intersection
        if (!IntersectAABB(node.bboxMin, node.bboxMax, ray, hit.t)) {
            continue;
        }
        
        // Leaf node - test triangles
        if (node.leftChild == -1) {
            for (int i = 0; i < node.primCount; ++i) {
                // Use the triangle index remap buffer to get the original GPU triangle index
                int remapIdx = node.firstPrim + i;
                int triIdx = g_bvhTriangleIndices[remapIdx];
                
                Triangle tri = g_triangles[triIdx];
                float t, u, v;
                
                if (RayTriangleIntersect(ray, tri, t, u, v)) {
                    if (t < hit.t && t > ray.tMin) {
                        hit.hit = true;
                        hit.t = t;
                        hit.position = ray.origin + ray.direction * t;
                        
                        float w = 1.0 - u - v;
                        hit.normal = normalize(w * tri.n0 + u * tri.n1 + v * tri.n2);
                        hit.texCoord = float2(u, v);
                        
                        // Clamp material index to valid range
                        hit.materialIndex = min(tri.materialIndex, numMaterials - 1);
                    }
                }
            }
        }
        // Internal node - push children
        else {
            // Push right child first (will be popped last, visited second)
            if (node.rightChild >= 0 && stackPtr < 31) {
                stack[stackPtr++] = node.rightChild;
            }
            // Push left child second (will be popped first, visited first)
            if (node.leftChild >= 0 && stackPtr < 31) {
                stack[stackPtr++] = node.leftChild;
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
            // Return environment light if enabled
            if (environmentLight > 0.0) {
                radiance += throughput * float3(environmentLight, environmentLight, environmentLight);
            }
            break;
        }
        
        // Ensure material index is valid
        uint matIdx = min(hit.materialIndex, numMaterials - 1);
        Material mat = g_materials[matIdx];
        
        // Direct light contribution from hit emissive surface
        if (mat.type == 4) {
            float3 emission = float3(mat.emission[0], mat.emission[1], mat.emission[2]);
            radiance += throughput * emission;
            break;
        }
        
        // Next Event Estimation: sample light source directly
        // Only do this for diffuse surfaces to avoid double counting with specular
        // TEMPORARILY DISABLED FOR DEBUGGING
        if (false && mat.type == 0 && bounce < 3) {
            // Find emissive material and sample it
            for (uint i = 0; i < numTriangles; i++) {
                Triangle lightTri = g_triangles[i];
                uint lightMatIdx = min(lightTri.materialIndex, numMaterials - 1);
                Material lightMat = g_materials[lightMatIdx];
                
                // Check if material is emissive
                if (lightMat.type == 4) { // Emissive material
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
                            float3 lightEmission = float3(lightMat.emission[0], lightMat.emission[1], lightMat.emission[2]);
                            
                            // Compute light area
                            float3 edge1 = lv1 - lv0;
                            float3 edge2 = lv2 - lv0;
                            float lightArea = length(cross(edge1, edge2)) * 0.5;
                            
                            // BRDF evaluation
                            float3 f = EvaluateBRDF(mat, -ray.direction, lightDir, hit.normal);
                            
                            // Direct lighting: L = Le * BRDF * cosTheta * cosTheta_light * Area / r^2
                            // PDF for uniform area sampling = 1/Area
                            // Contribution = Le * BRDF * cosTheta * cosTheta_light * Area / r^2 / (1/Area)
                            //              = Le * BRDF * cosTheta * cosTheta_light * Area^2 / r^2
                            float geometry = (NdotL * abs(LdotN)) / max(distToLight * distToLight, 0.0001);
                            radiance += throughput * f * lightEmission * geometry * lightArea;
                        }
                    }
                    break; // Only sample first light triangle
                }
            }
        }
        
        // Russian Roulette (start after a few bounces to ensure good sampling)
        if (bounce >= 3) {
            float p = max(0.05, max(throughput.r, max(throughput.g, throughput.b)));
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
    
    // Add random jitter for antialiasing
    float2 jitter = Random2D(rngState);
    float2 uv = (float2(pixelCoord) + jitter) / resolution;
    uv = uv * 2.0 - 1.0;
    uv.y = -uv.y;  // Flip Y to match camera space (Y-up)
    
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
    
    // Render new samples for this frame (samplesPerPixel is actually samples THIS frame)
    float3 newSamples = float3(0, 0, 0);
    for (uint i = 0; i < samplesPerPixel; ++i) {
        // Use global sample index to ensure unique seeds across all frames
        uint globalSampleIndex = accumulatedSamples + i;
        uint rngState = InitRNG(pixelCoord, frameIndex, globalSampleIndex);
        
        Ray ray = GenerateCameraRay(pixelCoord, rngState);
        newSamples += TracePath(ray, rngState);
    }
    // Don't divide by samplesPerPixel yet - accumulate raw samples
    
    // Load previous accumulated samples (linear HDR space)
    float4 prevAccum = g_accumulation[pixelCoord];
    
    // Add new samples to accumulation (stays in linear HDR)
    float3 totalAccum = prevAccum.rgb + newSamples;
    float totalSamples = prevAccum.a + float(samplesPerPixel);
    
    // Write updated accumulation
    g_accum_out[pixelCoord] = float4(totalAccum, totalSamples);
    
    // Compute averaged result for display
    float3 avgColor = totalAccum / max(1.0, totalSamples);
    
    // Apply tone mapping and gamma correction
    avgColor = avgColor / (avgColor + 1.0);
    avgColor = pow(abs(avgColor), 1.0 / 2.2);
    
    g_output[pixelCoord] = float4(avgColor, 1.0);
}
