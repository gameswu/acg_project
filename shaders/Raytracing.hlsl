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
    // Medium tracking (IOR stack) for nested refractive media
    float iorStack[4];    // Small stack for nested IORs (stack[0] = 1.0 = air)
    uint iorStackTop;     // Current top index
};

// Shadow ray payload (minimal structure for visibility queries)
struct ShadowPayload
{
    bool visible;  // True if ray reaches target without occlusion
};

// Global root signature
// #define GlobalRootSignature \
//     "DescriptorTable(UAV(u0))," /* Output texture */ \
//     "SRV(t0)," /* Acceleration Structure */ \
//     "SRV(t1)," /* Triangle vertices */ \
//     "SRV(t2)," /* Materials */ \
//     "SRV(t3)," /* Textures */ \
//     "CBV(b0)"  /* Scene constants */

// Scene constants structure
struct CameraConstants
{
    float4x4 viewInverse;
    float4x4 projInverse;
    uint frameIndex;
    uint maxBounces;
    float environmentLightIntensity;
    uint useVirtualTextures;  // 0 = texture array, 1 = virtual textures
    float4 cameraParams;      // x = FOV, y = aspectRatio, z = aperture, w = focusDistance
    float4 sunDirIntensity;   // xyz = sun direction, w = intensity
    float4 sunColorEnabled;   // rgb = sun color, a = enabled
};

// Scene constants (using new structure)
cbuffer SceneConstantBuffer : register(b0)
{
    CameraConstants g_constants;
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
Texture2DArray<float4> g_textures : register(t3);  // Standard texture array (fallback)
Texture2D<float4> g_environmentMap : register(t4);  // HDR environment map
Texture2D<float4> g_virtualTextureCache : register(t5);  // Virtual Texture physical page cache
Texture2DArray<uint> g_indirectionTexture : register(t6);  // Virtual Texture indirection lookup
StructuredBuffer<MaterialExtendedData> g_materialLayers : register(t7);  // Extended material layers
StructuredBuffer<float2> g_textureScales : register(t8);  // UV scale factors for resampled textures
StructuredBuffer<uint2> g_textureSizes : register(t9);  // Actual texture dimensions (width, height) per texture
SamplerState g_sampler : register(s0);

// Convert ray direction to equirectangular UV coordinates
float2 DirectionToEquirectangularUV(float3 dir)
{
    // Normalize direction
    dir = normalize(dir);
    
    // Convert to spherical coordinates
    // theta = azimuthal angle (0 to 2π)
    // phi = polar angle (0 to π)
    float phi = acos(dir.y);  // y is up
    float theta = atan2(dir.z, dir.x);  // atan2(z, x) for azimuth
    
    // Map to [0,1] UV space
    float u = theta / (2.0 * 3.14159265359) + 0.5;
    float v = phi / 3.14159265359;
    
    return float2(u, v);
}

// Helper function to create orthonormal basis from normal
void CreateOrthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    // Choose a helper vector that's not parallel to normal
    float3 helper = abs(normal.x) > 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    tangent = normalize(cross(normal, helper));
    bitangent = normalize(cross(normal, tangent)); // Ensure normalization
}

// Virtual Texture sampling helper
// Returns texture color using indirection-based lookup for virtual textures
float4 SampleVirtualTexture(int texIndex, float2 uv)
{
    // Virtual Texture system: 256x256 tile size
    const float TILE_SIZE = 256.0;
    
    // Validate texture index
    if (texIndex < 0 || texIndex >= 219) {
        // Invalid texture index - return error color (yellow)
        return float4(1.0, 1.0, 0.0, 1.0);
    }
    
    // Get actual texture dimensions from the buffer
    uint2 textureSizeUint = g_textureSizes[texIndex];
    float2 textureSize = float2(textureSizeUint.x, textureSizeUint.y);
    
    // CRITICAL: Wrap UV coordinates to [0, 1) to match sampler WRAP mode
    // This ensures we access valid tiles even when UV is tiled
    uv = frac(uv);
    
    // Clamp UV to prevent floating-point edge case where uv == 1.0 after frac
    // (can happen due to precision issues)
    uv = clamp(uv, 0.0, 0.999999);
    
    // Calculate which virtual tile this UV falls into
    float2 pixelCoords = uv * textureSize;
    float2 tileCoords = pixelCoords / TILE_SIZE;
    uint2 tileXY = uint2(floor(tileCoords));
    
    // Safety check: clamp tile coordinates to valid range
    uint numTilesX = (textureSizeUint.x + 255) / 256;  // Ceiling division
    uint numTilesY = (textureSizeUint.y + 255) / 256;
    tileXY.x = min(tileXY.x, numTilesX - 1);
    tileXY.y = min(tileXY.y, numTilesY - 1);
    
    // Lookup physical page index from indirection texture
    // Load uses pixel coordinates directly: (x, y, arrayIndex, mipLevel)
    uint physicalPageIndex = g_indirectionTexture.Load(int4(tileXY.x, tileXY.y, texIndex, 0));
    
    // Check if tile is resident (0xFFFFFFFF = not loaded)
    if (physicalPageIndex == 0xFFFFFFFF)
    {
        // Tile not resident - return white to indicate missing texture
        // (This allows us to see geometry shape even if texture is missing)
        return float4(0.8, 0.8, 0.8, 1.0);
    }
    
    // Validate physical page index to detect indexing errors
    const uint MAX_PHYSICAL_PAGES = 2304;  // Must match VirtualTextureSystem configuration
    if (physicalPageIndex >= MAX_PHYSICAL_PAGES)
    {
        // Invalid page index - return light gray for error
        return float4(0.5, 0.5, 0.5, 1.0);
    }
    
    // Calculate in-tile UV coordinates based on pixel position within the tile
    // This is more accurate than using frac(tileCoords) which can have precision issues
    float2 tileStartPixel = float2(tileXY) * TILE_SIZE;
    float2 pixelInTile = pixelCoords - tileStartPixel;
    float2 inTileUV = pixelInTile / TILE_SIZE;
    
    // CRITICAL: Clamp inTileUV to [0, 1) to avoid sampling outside tile bounds
    // This prevents bleeding between tiles in the physical cache
    inTileUV = clamp(inTileUV, 0.0, 0.999999);
    
    // Physical page cache layout: tiles arranged in a grid
    // Each page is 256x256, cache is arranged as sqrt(numPages) x sqrt(numPages)
    // For 2304 pages: 48 x 48 grid (12288x12288 texture = 576MB)
    const uint CACHE_TILES_PER_ROW = 48;  // sqrt(2304) = 48
    uint pageX = physicalPageIndex % CACHE_TILES_PER_ROW;
    uint pageY = physicalPageIndex / CACHE_TILES_PER_ROW;
    
    // Calculate final UV in physical cache texture
    // Each tile occupies (1 / CACHE_TILES_PER_ROW) of the UV space in each direction
    float tileUVSize = 1.0 / float(CACHE_TILES_PER_ROW);
    float2 tileBaseUV = float2(pageX, pageY) * tileUVSize;
    float2 finalCacheUV = tileBaseUV + inTileUV * tileUVSize;
    
    // Sample from physical cache
    float4 sampledColor = g_virtualTextureCache.SampleLevel(g_sampler, finalCacheUV, 0);
    
    return sampledColor;
}

    // ===================== Multiple Importance Sampling (MIS) Helpers =====================
    
    // Constants
    static const float PI = 3.14159265359;
    
    // Power heuristic for MIS weight computation (balance between sampling strategies)
    // pdf_a: PDF of strategy A, pdf_b: PDF of strategy B, beta: power parameter (typically 2)
    float PowerHeuristic(float pdf_a, float pdf_b, float beta = 2.0)
    {
        float wa = pow(abs(pdf_a), beta);
        float wb = pow(abs(pdf_b), beta);
        return wa / max(wa + wb, 1e-8);
    }
    
    // Balance heuristic (beta=1 case, simpler but less variance reduction)
    float BalanceHeuristic(float pdf_a, float pdf_b)
    {
        return pdf_a / max(pdf_a + pdf_b, 1e-8);
    }
    
    // Compute PDF for sampling a direction on equirectangular environment map
    // Accounts for Jacobian from spherical to texture space: sin(theta) term
    float EnvironmentMapPdf(float3 direction)
    {
        // Convert direction to spherical coordinates
        float theta = acos(clamp(direction.y, -1.0, 1.0));  // polar angle [0, pi]
        float sinTheta = sin(theta);
        
        // Uniform sampling over sphere: pdf = 1/(4*pi)
        // But equirectangular has non-uniform area: need Jacobian correction
        // Jacobian for equirectangular: 1/(2*pi*pi*sin(theta))
        // For uniform sampling: pdf = sin(theta) / (4*pi)
        float pdf = max(sinTheta, 1e-8) / (4.0 * PI);
        
        return pdf;
    }
    
    // Sample environment map direction uniformly (simplified importance sampling)
    // For production: use CDF-based importance sampling weighted by luminance
    float3 SampleEnvironmentMap(inout uint rngState, out float pdf)
    {
        // Uniform sampling on unit sphere using spherical coordinates
        float u1 = Random(rngState);
        float u2 = Random(rngState);
        
        // Sample theta (polar angle) with sin(theta) weighting for uniform area
        float cosTheta = 1.0 - 2.0 * u1;  // [-1, 1]
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        
        // Sample phi (azimuthal angle) uniformly
        float phi = 2.0 * PI * u2;
        
        // Convert to Cartesian coordinates
        float3 direction = float3(
            sinTheta * cos(phi),
            cosTheta,
            sinTheta * sin(phi)
        );
        
        // PDF for uniform sphere sampling
        pdf = EnvironmentMapPdf(direction);
        
        return normalize(direction);
    }
    
    // Trace shadow ray to test visibility between two points
    bool TraceShadowRay(float3 origin, float3 direction, float maxDistance)
    {
        RayDesc shadowRay;
        shadowRay.Origin = origin;
        shadowRay.Direction = direction;
        shadowRay.TMin = 0.001;  // Avoid self-intersection
        shadowRay.TMax = maxDistance - 0.001;  // Stop slightly before target
        
        ShadowPayload shadowPayload;
        shadowPayload.visible = true;
        
        // Shadow ray configuration:
        // - RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: terminate on first hit
        // - RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: don't need full hit shader for shadows
        // IndexOffsetMultiplier and GeometryContributionMultiplier should both be 0 for simple setup
        TraceRay(g_scene, 
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | 
                 RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
                 0xFF,  // Instance inclusion mask
                 0,     // RayContributionToHitGroupIndex (use 0, will be offset by multiplier)
                 1,     // MultiplierForGeometryContributionToHitGroupIndex (0*1 + 1 = HitGroup index 1)
                 1,     // MissShaderIndex (ShadowMiss at index 1)
                 shadowRay, 
                 shadowPayload);
        
        return shadowPayload.visible;
    }

    // ===================== Principled / Microfacet BSDF Helpers =====================
    // Interfaces (prototypes)

    float3 FresnelSchlick(float3 F0, float cosTheta);
    float D_GGX(float NdotH, float alpha);
    float G_Smith(float NdotV, float NdotL, float alpha);
    float3 Specular_GGX(float3 N, float3 V, float3 L, float roughness, float3 F0);
    float3 Diffuse_Burley(float3 albedo, float3 N, float3 V, float3 L);
    float SampleGGX_Direction(float3 N, float3 V, float roughness, inout uint rngState, out float3 sampledDir, out float pdf);
    void EvaluatePrincipledBSDF(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness, float3 F0, out float3 f, out float pdf);

    // Implementations
    float3 FresnelSchlick(float3 F0, float cosTheta)
    {
        cosTheta = saturate(cosTheta);
        return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
    }

    // GGX / Trowbridge-Reitz normal distribution function
    float D_GGX(float NdotH, float alpha)
    {
        float a2 = alpha * alpha;
        float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
        denom = PI * denom * denom;
        return a2 / max(1e-7, denom);
    }

    // Anisotropic GGX distribution
    float D_GGX_Anisotropic(float3 H, float3 N, float3 T, float3 B, float alphaX, float alphaY)
    {
        float NdotH = max(dot(N, H), 0.0);
        float TdotH = dot(T, H);
        float BdotH = dot(B, H);
        
        float a2 = alphaX * alphaY;
        float3 v = float3(alphaY * TdotH, alphaX * BdotH, a2 * NdotH);
        float v2 = dot(v, v);
        float w2 = a2 / v2;
        
        return a2 * w2 * w2 / PI;
    }

    // Schlick-GGX geometry term (Smith masking-shadowing)
    float G_Smith(float NdotV, float NdotL, float alpha)
    {
        // Schlick-GGX G1
        float k = (alpha + 1.0);
        k = (k * k) * 0.125; // (alpha+1)^2 / 8
        float G1V = NdotV / (NdotV * (1.0 - k) + k);
        float G1L = NdotL / (NdotL * (1.0 - k) + k);
        return G1V * G1L;
    }

    // Evaluate GGX specular term (returns RGB specular contribution)
    float3 Specular_GGX(float3 N, float3 V, float3 L, float roughness, float3 F0)
    {
        float3 H = normalize(V + L);
        float NdotV = max(dot(N, V), 0.0);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL <= 0.0 || NdotV <= 0.0) return float3(0,0,0);

        float alpha = max(0.001, roughness * roughness);
        float D = D_GGX(NdotH, alpha);
        float G = G_Smith(NdotV, NdotL, alpha);
        float3 F = FresnelSchlick(F0, VdotH);

        float denom = 4.0 * NdotV * NdotL + 1e-7;
        float3 spec = (D * G) / denom * F;
        return spec;
    }

    // Simple Burley diffuse (Disney diffuse approximation)
    float3 Diffuse_Burley(float3 albedo, float3 N, float3 V, float3 L)
    {
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        // For simplicity use Lambertian modulated by energy compensation
        // fd = albedo / PI * (1 + (1 - albedo) * 0.5 * (1 - NdotL) * (1 - NdotV)) -- simplified
        float3 base = albedo * (1.0 / PI);
        float fdmod = 1.0; // placeholder for more advanced Burley factor
        return base * fdmod;
    }

    // Sample GGX microfacet normal and return sampled outgoing direction (reflect V about H)
    float SampleGGX_Direction(float3 N, float3 V, float roughness, inout uint rngState, out float3 sampledDir, out float pdf)
    {
        // Sample H in N's tangent space using GGX sampling (approximate)
        float u1 = Random(rngState);
        float u2 = Random(rngState);

        float alpha = max(0.001, roughness * roughness);
        float phi = 2.0 * PI * u1;
        float tan2 = alpha * alpha * u2 / max(1e-7, (1.0 - u2));
        float cosTheta = 1.0 / sqrt(1.0 + tan2);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

        float3 Hlocal = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

        // Build basis around N
        float3 T, B;
        CreateOrthonormalBasis(N, T, B);
        float3 H = normalize(T * Hlocal.x + B * Hlocal.y + N * Hlocal.z);

        // Reflect view around H to get outgoing direction
        sampledDir = normalize(reflect(-V, H));

        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        // PDF for sampling H
        float alpha2 = alpha * alpha;
        float D = D_GGX(NdotH, alpha);
        float pdfH = D * NdotH;
        // convert pdf_H to pdf_L: pdf_L = pdf_H / (4 * VdotH)
        pdf = pdfH / max(1e-7, 4.0 * VdotH);

        // Return some metric (we won't use the return value directly)
        return pdf;
    }

    // Evaluate combined Principled BRDF (returns f and pdf for given directions)
    void EvaluatePrincipledBSDF(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness, float3 F0, out float3 f, out float pdf)
    {
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        if (NdotL <= 0.0 || NdotV <= 0.0) {
            f = float3(0,0,0);
            pdf = 0.0;
            return;
        }

        // Fresnel base reflectance for dielectrics
        float3 Fdielectric = F0; // caller should provide 0.04 for dielectric

        // Compute specular F0 for metals: use albedo as F0 for conductors
        float3 F0_metal = albedo;
        float3 F0_final = lerp(Fdielectric, F0_metal, metallic);

        // Specular term
        float3 spec = Specular_GGX(N, V, L, roughness, F0_final);

        // Diffuse term (zero for metals)
        float3 diff = (1.0 - metallic) * Diffuse_Burley(albedo, N, V, L);

        f = spec + diff;

        // Rough heuristic pdf: mix between specular and diffuse pdfs by metallic
        // Specular pdf from GGX sampling (approximated via half-vector)
        float3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        float alpha = max(0.001, roughness * roughness);
        float D = D_GGX(NdotH, alpha);
        float pdfSpec = (D * NdotH) / max(1e-7, 4.0 * VdotH);

        // Diffuse pdf (cosine hemisphere)
        float pdfDiff = NdotL / PI;

        // Mix pdf by probability of choosing specular vs diffuse (use metallic as proxy)
        float specProb = saturate(metallic);
        pdf = specProb * pdfSpec + (1.0 - specProb) * pdfDiff;
    }

    // Compute dielectric F0 from IOR using Fresnel at normal incidence
    float3 DielectricF0FromIOR(float ior)
    {
        float f = (ior - 1.0) / (ior + 1.0);
        float f0 = f * f;
        return float3(f0, f0, f0);
    }

    // Fetch material parameters: albedo, metallic, roughness, and compute F0
    // Uses virtual textures when enabled via g_constants.useVirtualTextures
    void GetMaterialParameters(in Material mat, in float2 uv, out float3 albedo, out float metallic, out float roughness, out float3 F0)
    {
        // Base values from material struct
        albedo = mat.baseColor();
        metallic = mat.metallic();
        roughness = mat.roughness();

        // Sample base color texture if present
        int baseIdx = mat.baseColorTexIdx();
        if (baseIdx >= 0) {
            float4 texColor;
            if (g_constants.useVirtualTextures == 1) texColor = SampleVirtualTexture(baseIdx, uv);
            else texColor = g_textures.SampleLevel(g_sampler, float3(uv, baseIdx), 0);
            albedo *= texColor.rgb;  // Modulate base color with texture (for Mix node workflows)
        }

        // Sample metallic-roughness texture if present (GLTF convention: G=roughness, B=metallic)
        int mrIdx = mat.metallicRoughnessTexIdx();
        if (mrIdx >= 0) {
            float4 mr;
            if (g_constants.useVirtualTextures == 1) mr = SampleVirtualTexture(mrIdx, uv);
            else mr = g_textures.SampleLevel(g_sampler, float3(uv, mrIdx), 0);
            // Interpret channels: G = roughness, B = metallic
            roughness = mr.g;
            metallic = mr.b;
        }

        // Compute F0: mix dielectric F0 (from IOR) and metallic F0 (use albedo)
        float3 Fdie = DielectricF0FromIOR(mat.ior());
        F0 = lerp(Fdie, albedo, saturate(metallic));
    }

    // ============================================================================
    // Volume Rendering - Homogeneous Volume Scattering
    // ============================================================================
    
    // Sample volume scattering distance using Beer's law (exponential distribution)
    // Returns true if scattering occurs within the ray segment, false if ray exits volume
    bool SampleVolumeScattering(float scatterDistance, float density, float rayLength, inout uint rngState, out float t)
    {
        // Extinction coefficient (inverse mean free path)
        float sigma_t = density / max(scatterDistance, 0.001f);
        
        // Sample distance from exponential distribution: t = -ln(1-xi) / sigma_t
        float xi = Random(rngState);
        t = -log(max(1.0 - xi, 1e-7)) / max(sigma_t, 1e-7);
        
        // Check if scattering occurs before ray exits volume
        return t < rayLength;
    }
    
    // Compute transmittance (Beer-Lambert law) for volume absorption
    float3 VolumeTransmittance(float3 absorptionColor, float density, float distance, float scatterDistance)
    {
        // Absorption coefficient (per-channel for colored absorption)
        // absorptionColor = 1 means no absorption, 0 means full absorption
        float3 sigma_a = (1.0 - absorptionColor) * density / max(scatterDistance, 0.001f);
        
        // Beer-Lambert: T = exp(-sigma_a * distance)
        return exp(-sigma_a * distance);
    }
    
    // Henyey-Greenstein phase function for volume scattering
    // g = anisotropy parameter: g=0 isotropic, g>0 forward, g<0 backward
    float PhaseHenyeyGreenstein(float cosTheta, float g)
    {
        float g2 = g * g;
        float denom = 1.0 + g2 - 2.0 * g * cosTheta;
        return (1.0 - g2) / (4.0 * PI * pow(abs(denom), 1.5));
    }
    
    // Sample Henyey-Greenstein phase function direction
    float3 SamplePhaseHG(float3 incomingDir, float g, inout uint rngState)
    {
        float xi1 = Random(rngState);
        float xi2 = Random(rngState);
        
        float cosTheta;
        if (abs(g) < 1e-3) {
            // Isotropic case
            cosTheta = 1.0 - 2.0 * xi1;
        } else {
            // Anisotropic case
            float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * xi1);
            cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
        }
        
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float phi = 2.0 * PI * xi2;
        
        // Build coordinate system aligned with incoming direction
        float3 w = -incomingDir; // Opposite of incoming for scattering
        float3 tangent, bitangent;
        CreateOrthonormalBasis(w, tangent, bitangent);
        
        // Construct scattered direction in local coordinates
        float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        return normalize(tangent * localDir.x + bitangent * localDir.y + w * localDir.z);
    }

    // ============================================================================
    // Sheen Layer - Fabric edge glow effect
    // ============================================================================
    
    // Sheen BRDF using inverted Fresnel (peak at grazing angles)
    // Based on "Production Friendly Microfacet Sheen BRDF" (Estevez & Kulla, 2017)
    float3 EvaluateSheen(float3 N, float3 V, float3 L, float3 sheenColor, float sheenRoughness)
    {
        float NdotV = max(dot(N, V), 1e-5);
        float NdotL = max(dot(N, L), 1e-5);
        
        // Half vector
        float3 H = normalize(V + L);
        float VdotH = max(dot(V, H), 0.0);
        
        // Inverted Fresnel: (1 - VdotH)^5 peaks at grazing angles
        float FH = pow(1.0 - VdotH, 5.0);
        
        // Simple sheen distribution (can use more sophisticated models)
        // Using inverted microfacet distribution for edge glow
        float sheen = FH * (1.0 + sheenRoughness);
        
        return sheenColor * sheen / max(NdotV + NdotL, 1e-5);
    }

    // ============================================================================
    // Subsurface Scattering - Simplified diffusion approximation
    // ============================================================================
    
    // Subsurface BRDF using wrap-around diffuse lighting
    // This is a simplified approximation - real SSS requires multi-bounce simulation
    float3 EvaluateSubsurface(float3 N, float3 V, float3 L, float3 sssColor, float sssRadius, float3 radiusScale)
    {
        float NdotL = dot(N, L);
        
        // Wrap-around lighting: allows light to "wrap" around edges
        // Simulates light penetrating and scattering inside the material
        float wrap = 0.5; // Amount of wrap (0.5 = moderate subsurface effect)
        float wrapNdotL = (NdotL + wrap) / ((1.0 + wrap) * (1.0 + wrap));
        wrapNdotL = max(wrapNdotL, 0.0);
        
        // Per-channel attenuation based on radius scale
        // Different wavelengths penetrate different depths (red goes deeper in skin)
        float3 attenuation = exp(-radiusScale / max(sssRadius, 0.001));
        
        // Subsurface diffuse with color filtering
        return sssColor * wrapNdotL * attenuation / PI;
    }

    // ============================================================================
    // Anisotropic Specular - Stretched highlights for brushed metal, hair, etc.
    // ============================================================================
    
    // Evaluate anisotropic GGX BRDF
    float3 EvaluateAnisotropicSpecular(float3 N, float3 V, float3 L, float3 tangent, float alphaX, float alphaY, float3 F0)
    {
        // Build anisotropic coordinate frame
        float3 T = normalize(tangent - N * dot(tangent, N)); // Ensure orthogonal to N
        float3 B = cross(N, T);
        
        float3 H = normalize(V + L);
        float NdotV = max(dot(N, V), 1e-5);
        float NdotL = max(dot(N, L), 1e-5);
        float VdotH = max(dot(V, H), 0.0);
        
        // Anisotropic GGX distribution
        float D = D_GGX_Anisotropic(H, N, T, B, alphaX, alphaY);
        
        // Geometry term (using average alpha for simplicity)
        float avgAlpha = (alphaX + alphaY) * 0.5;
        float G = G_Smith(NdotV, NdotL, avgAlpha);
        
        // Fresnel term
        float3 F = FresnelSchlick(F0, VdotH);
        
        // Cook-Torrance BRDF
        return (D * G * F) / max(1e-7, 4.0 * NdotV * NdotL);
    }

    // ============================================================================
    // Iridescence - Thin film interference (soap bubbles, oil slicks, CD discs)
    // ============================================================================
    
    // Compute thin film interference color based on viewing angle and thickness
    // Based on Abbe's sine condition and constructive/destructive interference
    float3 ThinFilmInterference(float cosTheta, float filmThickness, float filmIOR, float baseIOR)
    {
        // Optical path difference (OPD) through thin film
        // OPD = 2 * n * d * cos(theta_t) where theta_t is refracted angle
        float sinTheta2 = (baseIOR / filmIOR) * (baseIOR / filmIOR) * (1.0 - cosTheta * cosTheta);
        float cosTheta_t = sqrt(max(0.0, 1.0 - sinTheta2));
        float opd = 2.0 * filmIOR * filmThickness * cosTheta_t;
        
        // Phase shift for each wavelength (RGB approximation)
        // Visible spectrum: R~650nm, G~550nm, B~450nm
        float3 wavelengths = float3(650.0, 550.0, 450.0); // nanometers
        float3 phase = (2.0 * PI * opd) / wavelengths;
        
        // Constructive/destructive interference using cosine
        // cos(phase) = 1 for constructive, -1 for destructive
        float3 interference = (cos(phase) + 1.0) * 0.5; // Map [-1,1] to [0,1]
        
        return interference;
    }
    
    // Evaluate iridescence Fresnel with thin film modulation
    float3 FresnelIridescence(float3 F0, float cosTheta, float filmThickness, float filmIOR, float strength)
    {
        // Base Fresnel
        float3 baseF = FresnelSchlick(F0, cosTheta);
        
        // Thin film interference color
        float3 filmColor = ThinFilmInterference(cosTheta, filmThickness, filmIOR, 1.0);
        
        // Modulate Fresnel by film color
        float3 iridF = baseF * lerp(float3(1, 1, 1), filmColor, strength);
        
        return iridF;
    }


[shader("raygeneration")]
void RayGen()
{
    uint2 dispatchIdx = DispatchRaysIndex().xy;
    uint2 renderTargetSize = DispatchRaysDimensions().xy;

    // Initialize RNG for this pixel with per-sample variation
    // CRITICAL: Each sample must have different seed to generate different random sequences
    uint rngState = InitRNG(dispatchIdx, g_constants.frameIndex, g_constants.frameIndex * 17);
    
    // Subpixel jitter for anti-aliasing: sample uniformly inside pixel
    // Use per-pixel RNG to produce two independent jitter offsets in [0,1)
    // This implements industry-standard random subpixel sampling for AA.
    float jitterX = Random(rngState);
    float jitterY = Random(rngState);
    float2 pixelCenter = (float2)dispatchIdx + float2(jitterX, jitterY);
    float2 uv = pixelCenter / (float2)renderTargetSize; // [0,1]
    
    // Simple pinhole camera model
    // FOV is provided by CPU via g_constants.cameraParams.x (degrees). Use g_constants.cameraParams.y for aspect ratio.
    float aspectRatio = g_constants.cameraParams.y;
    float fov = g_constants.cameraParams.x * 3.14159265 / 180.0; // convert degrees -> radians
    float tanHalfFov = tan(fov * 0.5);
    
    // NDC coordinates ([-1,1] range)
    float2 ndc = uv * 2.0 - 1.0;
    ndc.x *= aspectRatio * tanHalfFov;
    ndc.y *= -tanHalfFov; // Flip Y
    
    // Get camera basis from g_constants.viewInverse matrix (ROW-MAJOR in HLSL)
    // Matrix is transposed on CPU, so in HLSL:
    // Row 0 = Right axis, Row 1 = Up axis, Row 2 = -Forward axis, Row 3 = Position
    float3 cameraRight = g_constants.viewInverse[0].xyz;     // First row
    float3 cameraUp = g_constants.viewInverse[1].xyz;        // Second row
    float3 cameraForward = -g_constants.viewInverse[2].xyz;  // Third row, camera looks along -Z
    float3 origin = g_constants.viewInverse[3].xyz;          // Fourth row - position
    
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
    // Initialize medium stack (start in air)
    payload.iorStack[0] = 1.0f;
    payload.iorStackTop = 0;
    
    // Iterative path tracing (multiple bounces)
    for (uint bounce = 0; bounce < g_constants.maxBounces && !payload.terminated; bounce++) {
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
    float3 rayDir = normalize(WorldRayDirection());
    
    // Sample environment map using equirectangular mapping
    float2 envUV = DirectionToEquirectangularUV(rayDir);
    float4 envColor = g_environmentMap.SampleLevel(g_sampler, envUV, 0);
    
    // Apply environment light intensity and add to radiance
    payload.radiance += payload.throughput * envColor.rgb * g_constants.environmentLightIntensity;

    // Add directional sun contribution for rays that reach infinity.
    // We evaluate sun radiance along the ray direction; if the ray direction
    // aligns with the sun direction, add sun contribution. Using intensity>0
    // as the enable check (g_constants.sunDirIntensity.w).
    if (g_constants.sunDirIntensity.w > 0.0) {
        float3 sunDir = normalize(g_constants.sunDirIntensity.xyz);
        float sunIntensity = g_constants.sunDirIntensity.w;
        float3 sunColor = g_constants.sunColorEnabled.rgb;

        // Alignment between ray direction and sun direction (1 when exactly aligned)
        float align = max(0.0, dot(rayDir, sunDir));
        if (align > 0.0) {
            // Add sun radiance seen along this direction
            payload.radiance += payload.throughput * sunColor * sunIntensity * align;
        }
    }

    payload.terminated = true;
}

// Shadow ray miss shader - ray reached environment without hitting geometry
[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    // Ray is visible - no occlusion
    payload.visible = true;
}

// Shadow ray any-hit shader - handle alpha-tested materials
[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Get material to check for alpha testing
    uint primitiveIndex = PrimitiveIndex();
    uint materialIndex = g_triangleMaterialIndices[primitiveIndex];
    
    const uint MAX_MATERIALS = 512;
    if (materialIndex >= MAX_MATERIALS) {
        materialIndex = 0;
    }
    
    Material mat = g_materials[materialIndex];
    
    // If material has opacity texture or alpha < 1, need to test
    float alpha = mat.opacity();
    int opacityTexIdx = mat.opacityTexIdx();
    
    if (opacityTexIdx >= 0 || alpha < 0.99) {
        // Interpolate texture coordinates
        uint i0 = g_indices[primitiveIndex * 3 + 0];
        uint i1 = g_indices[primitiveIndex * 3 + 1];
        uint i2 = g_indices[primitiveIndex * 3 + 2];
        
        float2 uv0 = g_vertices[i0].texCoord;
        float2 uv1 = g_vertices[i1].texCoord;
        float2 uv2 = g_vertices[i2].texCoord;
        
        float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y,
                                      attribs.barycentrics.x,
                                      attribs.barycentrics.y);
        
        float2 texCoord = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
        
        // Sample opacity texture if present
        if (opacityTexIdx >= 0) {
            float4 opacityTex;
            if (g_constants.useVirtualTextures == 1) {
                opacityTex = SampleVirtualTexture(opacityTexIdx, texCoord);
            } else {
                opacityTex = g_textures.SampleLevel(g_sampler, float3(texCoord, opacityTexIdx), 0);
            }
            alpha *= (opacityTex.r + opacityTex.g + opacityTex.b) / 3.0;
        }
        
        // Stochastic alpha testing for shadow rays
        // For shadows, we use deterministic threshold to avoid noise
        if (alpha < 0.5) {
            // Treat as transparent - ignore this hit and continue
            IgnoreHit();
        }
    }
    
    // If we reach here, surface is opaque - shadow ray is occluded
    payload.visible = false;
    AcceptHitAndEndSearch();
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Get primitive and material
    uint primitiveIndex = PrimitiveIndex();
    uint materialIndex = g_triangleMaterialIndices[primitiveIndex];
    
    // Bounds check for material index (287 materials for San Miguel)
    // If out of bounds, use material 0 as fallback
    const uint MAX_MATERIALS = 512;  // Conservative upper bound
    if (materialIndex >= MAX_MATERIALS) {
        materialIndex = 0;
    }
    
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
    
    // Use geometric normal for ray offset to avoid self-intersections
    float3 geometricNormal = faceNormal;
    // Use barycentric-weighted interpolated vertex normal for smooth shading
    float3 normal = interpolatedNormal;
    // Ensure the shading normal is oriented to the same hemisphere as the geometric normal
    if (dot(normal, faceNormal) < 0.0) {
        normal = -normal;
    }
    normal = normalize(normal);
    
    // Compute tangent space for anisotropic materials
    // Use UV derivatives to compute tangent and bitangent
    float2 deltaUV1 = uv1 - uv0;
    float2 deltaUV2 = uv2 - uv0;
    float r = 1.0 / max(abs(deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x), 1e-6);
    float3 tangent = normalize((edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r);
    // Gram-Schmidt orthogonalization to ensure tangent is perpendicular to normal
    tangent = normalize(tangent - normal * dot(tangent, normal));
    
    // Sample opacity/alpha texture if present
    float alpha = mat.opacity();
    int opacityTexIdx = mat.opacityTexIdx();
    if (opacityTexIdx >= 0) {
        float4 opacityTex;
        if (g_constants.useVirtualTextures == 1) {
            opacityTex = SampleVirtualTexture(opacityTexIdx, texCoord);
        } else {
            opacityTex = g_textures.SampleLevel(g_sampler, float3(texCoord, opacityTexIdx), 0);
        }
        
        // Use average of RGB channels or alpha channel as opacity
        // For grayscale opacity maps, all RGB channels should be same
        alpha *= (opacityTex.r + opacityTex.g + opacityTex.b) / 3.0;
    }
    
    // Alpha test for transparent/cutout materials
    // For clouds, we need stochastic alpha testing to create volume-like appearance
    if (alpha < 0.99) {
        // Stochastic alpha testing: randomly terminate ray based on alpha value
        // This creates a volume-like appearance through probabilistic transparency
        float alphaRand = Random(payload.rngState);
        
        // Softer threshold for cloud-like materials (SSS + low alpha)
        // Use alpha^2 to make transition more gradual
        float alphaThreshold = alpha * alpha;
        
        if (alphaRand > alphaThreshold) {
            // Ray passes through - continue in same direction
            payload.nextOrigin = hitPos + rayDir * 0.001;
            payload.nextDirection = rayDir;
            return;
        }
    }
    
    // Add emission from this surface
    payload.radiance += payload.throughput * mat.emission();
    
    // If this is an emissive surface, terminate the path
    float emissionMagnitude = dot(mat.emission(), float3(1, 1, 1));
    if (emissionMagnitude > 0.01) {
        payload.terminated = true;
        return;
    }

    // Sun contribution for surface shading should be handled via explicit visibility
    // tests or by evaluating environment on miss rays. Per requirement, we DO NOT add
    // sun contribution here. Sun is applied when a ray reaches infinity (in the Miss shader).
    
    // ========== MATERIAL TYPE CLASSIFICATION ==========
    // New PBR material system: use layer flags and properties
    
    // Check for volume layer (participatory media like fog/smoke/clouds)
    bool hasVolume = (mat.layerFlags() & LAYER_VOLUME) != 0;
    
    // Check for transmission layer (glass/transparent materials)
    bool hasTransmission = (mat.layerFlags() & LAYER_TRANSMISSION) != 0;
    
    // Handle volume rendering (highest priority - affects ray path inside volumes)
    if (hasVolume) {
        // Load volume properties from extended data
        // CRITICAL: Calculate correct layer index based on layer flags ordering
        uint layerIdx = mat.extendedDataIndex();
        uint flags = mat.layerFlags();
        
        // Count layers before VOLUME (0x40) to compute offset
        // Layer order in Scene::CollectAllMaterialLayers():
        // Clearcoat(0x01) -> Transmission(0x02) -> Sheen(0x04) -> Subsurface(0x08) -> Anisotropy(0x10) -> Iridescence(0x20) -> Volume(0x40)
        uint layerOffset = 0;
        if (flags & LAYER_CLEARCOAT) layerOffset++;
        if (flags & LAYER_TRANSMISSION) layerOffset++;
        if (flags & LAYER_SHEEN) layerOffset++;
        if (flags & LAYER_SUBSURFACE) layerOffset++;
        if (flags & LAYER_ANISOTROPY) layerOffset++;
        if (flags & LAYER_IRIDESCENCE) layerOffset++;
        
        VolumeLayer vol = LoadVolumeLayer(layerIdx + layerOffset, g_materialLayers);
        
        // Sample scattering distance
        float scatterDist;
        bool scattered = SampleVolumeScattering(vol.scatterDistance, vol.density, t, payload.rngState, scatterDist);
        
        if (scattered) {
            // Scattering event inside volume
            float3 scatterPos = rayOrigin + rayDir * scatterDist;
            
            // Apply transmittance up to scattering point
            float3 transmittance = VolumeTransmittance(vol.absorptionColor, vol.density, scatterDist, vol.scatterDistance);
            payload.throughput *= transmittance * vol.scatterColor;
            
            // Sample phase function for new direction (using isotropic scattering g=0)
            float3 newDir = SamplePhaseHG(rayDir, 0.0, payload.rngState);
            
            // Continue ray from scattering position
            payload.nextOrigin = scatterPos;
            payload.nextDirection = newDir;
            
            // Don't terminate - continue tracing through volume
            return;
        } else {
            // No scattering - ray exits volume at surface
            // Apply full transmittance through volume segment
            float3 transmittance = VolumeTransmittance(vol.absorptionColor, vol.density, t, vol.scatterDistance);
            payload.throughput *= transmittance;
            
            // Continue with surface interaction (fall through to transmission/BSDF)
        }
    }
    
    // Handle refractive/transmissive materials
    if (hasTransmission) {
        // Get base color (with texture support for colored glass)
        float3 baseColor = mat.baseColor();
        int baseColorIdx = mat.baseColorTexIdx();
        if (baseColorIdx >= 0) {
            float4 texColor;
            if (g_constants.useVirtualTextures == 1) {
                texColor = SampleVirtualTexture(baseColorIdx, texCoord);
            } else {
                texColor = g_textures.SampleLevel(g_sampler, float3(texCoord, baseColorIdx), 0);
            }
            baseColor = texColor.rgb;
        }
        
        // Glass material with reflection and refraction
        float ior = mat.ior();
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

        // Use opacity for transmission: opacity=1 is opaque, opacity=0 is fully transmissive
        // For transmission, we want the inverse: trans = 1 - opacity
        float trans = 1.0 - mat.opacity();
        if (trans <= 0.001f) {
            // Derive a conservative fallback from base color
            float3 baseColorVal = mat.baseColor();
            float avgColor = (baseColorVal.r + baseColorVal.g + baseColorVal.b) * (1.0 / 3.0);
            trans = max(avgColor, 0.01f);
        }

        // Compute small offset to avoid self-intersections
        float eps = 0.001f;
        float3 offset = geometricNormal * eps;

        if (rand < fresnel || k < 0.0) {
            // Total internal reflection or Fresnel reflection
            float3 reflectDir = reflect(rayDir, N);
            // Use geometric normal for consistent offset direction
            bool entering = dot(rayDir, geometricNormal) < 0.0;
            payload.nextOrigin = entering ? hitPos - offset : hitPos + offset;
            payload.nextDirection = reflectDir;
            // throughput unchanged except by reflectance (handled elsewhere)
        } else {
            // Refraction
            // Determine current and target IOR from payload stack
            float iorCurrent = payload.iorStack[payload.iorStackTop];
            // Use geometric normal for robust entering/exiting detection
            bool entering = dot(rayDir, geometricNormal) < 0.0; // entering if ray goes against geometric normal
            float iorTarget = entering ? mat.ior() : (payload.iorStackTop > 0 ? payload.iorStack[payload.iorStackTop - 1] : 1.0f);

            float etaLocal = iorCurrent / iorTarget;
            float kLocal = 1.0 - etaLocal * etaLocal * (1.0 - cosI * cosI);
            // If total internal reflection detected here, fallback to reflection
            if (kLocal < 0.0) {
                float3 reflectDir = reflect(rayDir, N);
                // Always offset away from surface along geometric normal
                payload.nextOrigin = entering ? hitPos - offset : hitPos + offset;
                payload.nextDirection = reflectDir;
            } else {
                float3 refractDir = etaLocal * rayDir + (etaLocal * cosI - sqrt(kLocal)) * N;
                payload.nextDirection = refractDir;
                // Always offset away from surface along geometric normal
                payload.nextOrigin = entering ? hitPos - offset : hitPos + offset;

                // Update IOR stack: push on entering, pop on exiting
                if (entering) {
                    uint newTop = min(payload.iorStackTop + 1, 3);
                    payload.iorStackTop = newTop;
                    payload.iorStack[payload.iorStackTop] = mat.ior();
                } else {
                    if (payload.iorStackTop > 0) payload.iorStackTop--;
                }

                // Apply color filtering (use texture-modified base color) and transmission amount
                payload.throughput *= baseColor * trans;
            }
        }
        return;
    }
    // Use Principled BSDF sampling/evaluation for non-transmission materials
    else {
        // Check for clearcoat layer
        bool hasClearcoat = (mat.layerFlags() & LAYER_CLEARCOAT) != 0;
        
        // Fetch material parameters (albedo, metallic, roughness, F0)
        float3 albedo;
        float metallic;
        float roughness;
        float3 F0;
        GetMaterialParameters(mat, texCoord, albedo, metallic, roughness, F0);

        // View vector (pointing from surface toward camera)
        float3 V = -rayDir;
        
        // ========== CLEARCOAT LAYER HANDLING ==========
        if (hasClearcoat) {
            // Load clearcoat properties
            // Clearcoat is always first layer (offset = 0)
            uint layerIdx = mat.extendedDataIndex();
            ClearcoatLayer coat = LoadClearcoatLayer(layerIdx, g_materialLayers);
            
            // Compute clearcoat Fresnel weight at current angle
            float NdotV = max(dot(normal, V), 0.0);
            float F0_coat = ((coat.ior - 1.0) / (coat.ior + 1.0));
            F0_coat = F0_coat * F0_coat;
            float fresnel_coat = F0_coat + (1.0 - F0_coat) * pow(1.0 - NdotV, 5.0);
            float coatWeight = coat.strength * fresnel_coat;
            
            // Use Russian Roulette to choose between clearcoat and base layer
            float clearcoatProb = saturate(coatWeight);
            float rClearcoat = Random(payload.rngState);
            
            if (rClearcoat < clearcoatProb) {
                // ===== Sample clearcoat layer (specular GGX) =====
                float3 sampledDir;
                float samplePdf;
                SampleGGX_Direction(normal, V, coat.roughness, payload.rngState, sampledDir, samplePdf);
                
                float NdotL = max(dot(normal, sampledDir), 0.0);
                if (NdotL <= 0.0 || samplePdf <= 0.0) {
                    payload.terminated = true;
                    return;
                }
                
                // Evaluate clearcoat specular BRDF (GGX with clearcoat IOR)
                float3 coatF0 = float3(F0_coat, F0_coat, F0_coat);
                float3 coatBRDF = Specular_GGX(normal, V, sampledDir, coat.roughness, coatF0);
                
                // Apply clearcoat tint
                coatBRDF *= coat.tint;
                
                // Update throughput: f * cosTheta / pdf / selectionProb
                payload.throughput *= coatBRDF * NdotL / samplePdf / clearcoatProb;
                
                payload.nextOrigin = hitPos + geometricNormal * 0.001;
                payload.nextDirection = sampledDir;
                return;
            } else {
                // ===== Sample base layer (with clearcoat attenuation) =====
                // Energy that passes through clearcoat reduces base layer contribution
                float baseProb = 1.0 - clearcoatProb;
                
                // Continue to base layer sampling (same as before, but adjust throughput)
                // The base layer sees less energy due to clearcoat reflection
                // This is implicitly handled by the Russian Roulette probability
                // We'll adjust throughput by 1/baseProb to account for selection probability
                
                // [Fall through to base layer sampling below]
                // Adjust initial throughput factor
                float baseThroughputFactor = 1.0 / max(baseProb, 0.01);
                payload.throughput *= (1.0 - coatWeight) * baseThroughputFactor;
            }
        }
        
        // ========== BASE LAYER SAMPLING (MIS TEMPORARILY DISABLED) ==========
        // TODO: Debug and re-enable MIS after verifying base path tracing works
        
        // For now, use original single-strategy BRDF sampling without shadow rays
        float3 directLighting = float3(0, 0, 0);  // No explicit direct lighting for now
        
        #if 0  // Disable entire MIS block
        // ========== BASE LAYER SAMPLING WITH MIS ==========
        // Use Multiple Importance Sampling combining BRDF and environment light sampling
        
        float3 directLighting = float3(0, 0, 0);
        
        // ===== Strategy 1: BRDF Importance Sampling (TEMPORARILY DISABLED FOR DEBUGGING) =====
        // TODO: Re-enable after verifying shadow ray performance
        #if 0
        {
            // Decide between specular (GGX) and diffuse sampling
            float specProb = saturate(metallic + (1.0 - roughness) * (1.0 - metallic));
            float r = Random(payload.rngState);

            float3 sampledDir;
            float brdfPdf = 0.0;
            float selectionProb = 1.0;

            if (r < specProb) {
                // Sample GGX specular lobe
                SampleGGX_Direction(normal, V, roughness, payload.rngState, sampledDir, brdfPdf);
                selectionProb = specProb;
            } else {
                // Cosine-weighted hemisphere (diffuse)
                float r1 = Random(payload.rngState);
                float r2 = Random(payload.rngState);
                float sinTheta = sqrt(r1);
                float cosTheta = sqrt(1.0 - r1);
                float phi = 2.0 * PI * r2;

                float3 tangent, bitangent;
                CreateOrthonormalBasis(normal, tangent, bitangent);
                float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
                sampledDir = normalize(tangent * localDir.x + bitangent * localDir.y + normal * localDir.z);
                brdfPdf = max(dot(normal, sampledDir), 0.0) / PI;
                selectionProb = max(1.0 - specProb, 0.0);
            }

            float NdotL = max(dot(normal, sampledDir), 0.0);
            if (NdotL > 0.0 && brdfPdf > 0.0) {
                // Evaluate BRDF
                float3 f;
                float dummyPdf;
                EvaluatePrincipledBSDF(normal, V, sampledDir, albedo, metallic, roughness, F0, f, dummyPdf);
                
                // Apply material layers to BRDF (same as indirect bounces)
                // Iridescence
                if ((mat.layerFlags() & LAYER_IRIDESCENCE) != 0) {
                    uint layerIdx = mat.extendedDataIndex();
                    uint flags = mat.layerFlags();
                    uint layerOffset = 0;
                    if (flags & LAYER_CLEARCOAT) layerOffset++;
                    if (flags & LAYER_TRANSMISSION) layerOffset++;
                    if (flags & LAYER_SHEEN) layerOffset++;
                    if (flags & LAYER_SUBSURFACE) layerOffset++;
                    if (flags & LAYER_ANISOTROPY) layerOffset++;
                    IridescenceLayer irid = LoadIridescenceLayer(layerIdx + layerOffset, g_materialLayers);
                    float thickness = (irid.thicknessMin + irid.thicknessMax) * 0.5;
                    float NdotV = max(dot(normal, V), 0.0);
                    float3 iridF = FresnelIridescence(F0, NdotV, thickness, irid.ior, irid.strength);
                    float3 baseF = FresnelSchlick(F0, NdotV);
                    float3 fresnelRatio = iridF / max(baseF, 1e-5);
                    float iridWeight = saturate(metallic + (1.0 - roughness) * 0.5);
                    f *= lerp(float3(1, 1, 1), fresnelRatio, iridWeight * irid.strength);
                }
                // Sheen
                if ((mat.layerFlags() & LAYER_SHEEN) != 0) {
                    uint layerIdx = mat.extendedDataIndex();
                    uint flags = mat.layerFlags();
                    uint layerOffset = 0;
                    if (flags & LAYER_CLEARCOAT) layerOffset++;
                    if (flags & LAYER_TRANSMISSION) layerOffset++;
                    SheenLayer sheen = LoadSheenLayer(layerIdx + layerOffset, g_materialLayers);
                    f += EvaluateSheen(normal, V, sampledDir, sheen.color, sheen.roughness) * sheen.tint;
                }
                // Subsurface
                if ((mat.layerFlags() & LAYER_SUBSURFACE) != 0) {
                    uint layerIdx = mat.extendedDataIndex();
                    uint flags = mat.layerFlags();
                    uint layerOffset = 0;
                    if (flags & LAYER_CLEARCOAT) layerOffset++;
                    if (flags & LAYER_TRANSMISSION) layerOffset++;
                    if (flags & LAYER_SHEEN) layerOffset++;
                    SubsurfaceLayer sss = LoadSubsurfaceLayer(layerIdx + layerOffset, g_materialLayers);
                    float3 sssBRDF = EvaluateSubsurface(normal, V, sampledDir, sss.color, sss.radius, sss.radiusScale);
                    float3 baseDiffuse = (1.0 - metallic) * Diffuse_Burley(albedo, normal, V, sampledDir);
                    float3 specular = Specular_GGX(normal, V, sampledDir, roughness, F0);
                    float3 mixedDiffuse = lerp(baseDiffuse, sssBRDF, saturate(sss.strength));
                    float specularWeight = saturate(1.0 - sss.strength * 0.8);
                    f = specular * specularWeight + mixedDiffuse;
                }
                // Anisotropy
                if ((mat.layerFlags() & LAYER_ANISOTROPY) != 0) {
                    uint layerIdx = mat.extendedDataIndex();
                    uint flags = mat.layerFlags();
                    uint layerOffset = 0;
                    if (flags & LAYER_CLEARCOAT) layerOffset++;
                    if (flags & LAYER_TRANSMISSION) layerOffset++;
                    if (flags & LAYER_SHEEN) layerOffset++;
                    if (flags & LAYER_SUBSURFACE) layerOffset++;
                    AnisotropyLayer aniso = LoadAnisotropyLayer(layerIdx + layerOffset, g_materialLayers);
                    float aspect = aniso.aspectRatio;
                    float alphaX = roughness;
                    float alphaY = roughness * aspect;
                    float cosRot = cos(aniso.rotation);
                    float sinRot = sin(aniso.rotation);
                    float3 rotTangent = tangent * cosRot + cross(normal, tangent) * sinRot;
                    float3 anisoTangent = length(aniso.tangent) > 0.1 ? aniso.tangent : rotTangent;
                    float3 anisoSpec = EvaluateAnisotropicSpecular(normal, V, sampledDir, anisoTangent, alphaX, alphaY, F0);
                    float3 isoSpec = Specular_GGX(normal, V, sampledDir, roughness, F0);
                    float3 finalSpec = lerp(isoSpec, anisoSpec, aniso.strength);
                    float3 diffuse = (1.0 - metallic) * Diffuse_Burley(albedo, normal, V, sampledDir);
                    f = finalSpec + diffuse;
                }
                
                // Trace shadow ray to test visibility
                bool visible = TraceShadowRay(hitPos + geometricNormal * 0.001, sampledDir, 10000.0);
                
                if (visible) {
                    // Sample environment map at this direction
                    float2 envUV = DirectionToEquirectangularUV(sampledDir);
                    float3 envRadiance = g_environmentMap.SampleLevel(g_sampler, envUV, 0).rgb * g_constants.environmentLightIntensity;
                    
                    // Compute light PDF for this direction
                    float lightPdf = EnvironmentMapPdf(sampledDir);
                    
                    // MIS weight: power heuristic with beta=2
                    float misWeight = PowerHeuristic(brdfPdf, lightPdf);
                    
                    // Contribution: f * L * cos(theta) * weight / pdf
                    directLighting += f * envRadiance * NdotL * misWeight / (brdfPdf * selectionProb);
                }
            }
        }
        #endif // Strategy 1 disabled
        
        // ===== Strategy 2: Environment Light Importance Sampling =====
        // This strategy samples light first, then evaluates BRDF
        {
            float lightPdf;
            float3 lightDir = SampleEnvironmentMap(payload.rngState, lightPdf);
            
            float NdotL = max(dot(normal, lightDir), 0.0);
            if (NdotL > 0.0 && lightPdf > 0.0) {
                // Trace shadow ray
                bool visible = TraceShadowRay(hitPos + geometricNormal * 0.001, lightDir, 10000.0);
                
                if (visible) {
                    // Evaluate BRDF for this direction
                    float3 f;
                    float brdfPdf;
                    EvaluatePrincipledBSDF(normal, V, lightDir, albedo, metallic, roughness, F0, f, brdfPdf);
                    
                    // Sample environment radiance
                    float2 envUV = DirectionToEquirectangularUV(lightDir);
                    float3 envRadiance = g_environmentMap.SampleLevel(g_sampler, envUV, 0).rgb * g_constants.environmentLightIntensity;
                    
                    // MIS weight: power heuristic with beta=2
                    float misWeight = PowerHeuristic(lightPdf, brdfPdf);
                    
                    // Contribution: f * L * cos(theta) * weight / pdf
                    directLighting += f * envRadiance * NdotL * misWeight / lightPdf;
                }
            }
        }
        #endif  // End MIS block
        
        // Add direct lighting contribution (currently zero - will be lit by indirect bounces only)
        payload.radiance += payload.throughput * directLighting;
        
        // ===== Continue path with BRDF sampling for next bounce =====
        // Original path tracing logic without MIS
        float specProb = saturate(metallic + (1.0 - roughness) * (1.0 - metallic));
        float r = Random(payload.rngState);

        float3 sampledDir;
        float samplePdf = 0.0;
        float selectionProb = 1.0;

        if (r < specProb) {
            SampleGGX_Direction(normal, V, roughness, payload.rngState, sampledDir, samplePdf);
            selectionProb = specProb;
        } else {
            float r1 = Random(payload.rngState);
            float r2 = Random(payload.rngState);
            float sinTheta = sqrt(r1);
            float cosTheta = sqrt(1.0 - r1);
            float phi = 2.0 * PI * r2;

            float3 tangent, bitangent;
            CreateOrthonormalBasis(normal, tangent, bitangent);
            float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
            sampledDir = normalize(tangent * localDir.x + bitangent * localDir.y + normal * localDir.z);
            samplePdf = max(dot(normal, sampledDir), 0.0) / PI;
            selectionProb = max(1.0 - specProb, 0.0);
        }

        float NdotL = max(dot(normal, sampledDir), 0.0);
        if (NdotL <= 0.0 || samplePdf <= 0.0) {
            payload.terminated = true;
            return;
        }

        float3 f;
        float dummyPdf;
        EvaluatePrincipledBSDF(normal, V, sampledDir, albedo, metallic, roughness, F0, f, dummyPdf);
        
        // ========== APPLY IRIDESCENCE TO FRESNEL ==========
        bool hasIridescence = (mat.layerFlags() & LAYER_IRIDESCENCE) != 0;
        if (hasIridescence) {
            // Load iridescence properties
            uint layerIdx = mat.extendedDataIndex();
            uint flags = mat.layerFlags();
            uint layerOffset = 0;
            if (flags & LAYER_CLEARCOAT) layerOffset++;
            if (flags & LAYER_TRANSMISSION) layerOffset++;
            if (flags & LAYER_SHEEN) layerOffset++;
            if (flags & LAYER_SUBSURFACE) layerOffset++;
            if (flags & LAYER_ANISOTROPY) layerOffset++;
            // Iridescence is 6th layer in order
            IridescenceLayer irid = LoadIridescenceLayer(layerIdx + layerOffset, g_materialLayers);
            
            // Use average thickness (can sample texture for variation)
            float thickness = (irid.thicknessMin + irid.thicknessMax) * 0.5;
            
            // Compute view-dependent iridescent Fresnel
            float NdotV = max(dot(normal, V), 0.0);
            float3 iridF = FresnelIridescence(F0, NdotV, thickness, irid.ior, irid.strength);
            
            // Modulate specular component by iridescent Fresnel
            // Approximate: scale entire BRDF by Fresnel ratio (iridF / baseF)
            float3 baseF = FresnelSchlick(F0, NdotV);
            float3 fresnelRatio = iridF / max(baseF, 1e-5);
            
            // Apply to specular-like materials (high metallic/low roughness)
            float iridWeight = saturate(metallic + (1.0 - roughness) * 0.5);
            f *= lerp(float3(1, 1, 1), fresnelRatio, iridWeight * irid.strength);
        }
        
        // ========== ADD SHEEN LAYER CONTRIBUTION ==========
        bool hasSheen = (mat.layerFlags() & LAYER_SHEEN) != 0;
        if (hasSheen) {
            // Load sheen properties
            uint layerIdx = mat.extendedDataIndex();
            uint flags = mat.layerFlags();
            uint layerOffset = 0;
            if (flags & LAYER_CLEARCOAT) layerOffset++;
            if (flags & LAYER_TRANSMISSION) layerOffset++;
            // Sheen is 3rd layer in order
            SheenLayer sheen = LoadSheenLayer(layerIdx + layerOffset, g_materialLayers);
            
            // Evaluate sheen BRDF and add to base BRDF
            float3 sheenBRDF = EvaluateSheen(normal, V, sampledDir, sheen.color, sheen.roughness);
            
            // Apply sheen tint
            sheenBRDF *= sheen.tint;
            
            // Add sheen contribution to total BRDF
            f += sheenBRDF;
        }
        
        // ========== ADD SUBSURFACE SCATTERING CONTRIBUTION ==========
        bool hasSubsurface = (mat.layerFlags() & LAYER_SUBSURFACE) != 0;
        if (hasSubsurface) {
            // Load subsurface properties
            uint layerIdx = mat.extendedDataIndex();
            uint flags = mat.layerFlags();
            uint layerOffset = 0;
            if (flags & LAYER_CLEARCOAT) layerOffset++;
            if (flags & LAYER_TRANSMISSION) layerOffset++;
            if (flags & LAYER_SHEEN) layerOffset++;
            // Subsurface is 4th layer in order
            SubsurfaceLayer sss = LoadSubsurfaceLayer(layerIdx + layerOffset, g_materialLayers);
            
            // Evaluate subsurface BRDF
            float3 sssBRDF = EvaluateSubsurface(normal, V, sampledDir, sss.color, sss.radius, sss.radiusScale);
            
            // Mix subsurface with base BRDF based on Subsurface Weight
            // When strength=0, use base BRDF; when strength=1, use pure SSS
            float3 baseDiffuse = (1.0 - metallic) * Diffuse_Burley(albedo, normal, V, sampledDir);
            float3 specular = Specular_GGX(normal, V, sampledDir, roughness, F0);
            
            // Replace diffuse component with SSS based on strength
            float3 mixedDiffuse = lerp(baseDiffuse, sssBRDF, saturate(sss.strength));
            
            // For high SSS strength (cloud-like materials), reduce specular contribution
            // This prevents mirror-like appearance on volumetric surfaces
            float specularWeight = saturate(1.0 - sss.strength * 0.8);
            f = specular * specularWeight + mixedDiffuse;
        }
        
        // ========== MODIFY SPECULAR FOR ANISOTROPY ==========
        bool hasAnisotropy = (mat.layerFlags() & LAYER_ANISOTROPY) != 0;
        if (hasAnisotropy) {
            // Load anisotropy properties
            uint layerIdx = mat.extendedDataIndex();
            uint flags = mat.layerFlags();
            uint layerOffset = 0;
            if (flags & LAYER_CLEARCOAT) layerOffset++;
            if (flags & LAYER_TRANSMISSION) layerOffset++;
            if (flags & LAYER_SHEEN) layerOffset++;
            if (flags & LAYER_SUBSURFACE) layerOffset++;
            // Anisotropy is 5th layer in order
            AnisotropyLayer aniso = LoadAnisotropyLayer(layerIdx + layerOffset, g_materialLayers);
            
            // Compute anisotropic roughness parameters
            float aspect = aniso.aspectRatio;
            float alphaX = roughness;
            float alphaY = roughness * aspect;
            
            // Rotate tangent by anisotropy rotation
            float cosRot = cos(aniso.rotation);
            float sinRot = sin(aniso.rotation);
            float3 rotTangent = tangent * cosRot + cross(normal, tangent) * sinRot;
            
            // Use anisotropic tangent from material if provided, otherwise use computed
            float3 anisoTangent = length(aniso.tangent) > 0.1 ? aniso.tangent : rotTangent;
            
            // Evaluate anisotropic specular BRDF
            float3 anisoSpec = EvaluateAnisotropicSpecular(normal, V, sampledDir, anisoTangent, alphaX, alphaY, F0);
            
            // Blend anisotropic specular with isotropic based on anisotropy strength
            float3 isoSpec = Specular_GGX(normal, V, sampledDir, roughness, F0);
            float3 finalSpec = lerp(isoSpec, anisoSpec, aniso.strength);
            
            // Replace specular component in BRDF
            // Extract diffuse component (approximate: f - specular)
            float3 diffuse = (1.0 - metallic) * Diffuse_Burley(albedo, normal, V, sampledDir);
            f = finalSpec + diffuse;
        }

        // Update throughput: multiply by contribution = f * cosTheta / pdf
        // Also divide by the probability of choosing this sampling branch (selectionProb)
        selectionProb = max(selectionProb, 1e-6); // protect against zero
        payload.throughput *= f * NdotL / (samplePdf * selectionProb);

        // Russian roulette termination (if throughput very small)
        float maxThroughput = max(max(payload.throughput.r, payload.throughput.g), payload.throughput.b);
        if (maxThroughput < 0.001) {
            payload.terminated = true;
            return;
        }

        payload.nextOrigin = hitPos + geometricNormal * 0.001;
        payload.nextDirection = sampledDir;
        return;
    }
}