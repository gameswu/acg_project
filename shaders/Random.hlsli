// Random number generation utilities
#ifndef RANDOM_HLSLI
#define RANDOM_HLSLI

// PCG hash
uint PCGHash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Initialize RNG state with sample offset for better distribution
uint InitRNG(uint2 pixelCoord, uint frameIndex, uint sampleIndex) {
    uint seed = pixelCoord.x + pixelCoord.y * 19349663u + frameIndex * 83492791u + sampleIndex * 3141592653u;
    return PCGHash(seed);
}

// Initialize RNG state (legacy version)
uint InitRNG(uint2 pixelCoord, uint frameIndex) {
    return PCGHash(pixelCoord.x + PCGHash(pixelCoord.y + PCGHash(frameIndex)));
}

// Generate random float [0, 1)
float Random(inout uint state) {
    state = PCGHash(state);
    return float(state) / 4294967296.0;
}

// Generate random float2
float2 Random2D(inout uint state) {
    return float2(Random(state), Random(state));
}

// Generate random float3
float3 Random3D(inout uint state) {
    return float3(Random(state), Random(state), Random(state));
}

// Cosine-weighted hemisphere sampling
float3 CosineSampleHemisphere(float2 u) {
    float r = sqrt(u.x);
    float theta = 2.0 * 3.14159265359 * u.y;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u.x));
    return float3(x, y, z);
}

// Uniform hemisphere sampling
float3 UniformSampleHemisphere(float2 u) {
    float phi = 2.0 * 3.14159265359 * u.x;
    float cosTheta = u.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

// Build orthonormal basis from normal
void CreateCoordinateSystem(float3 normal, out float3 tangent, out float3 bitangent) {
    if (abs(normal.x) > abs(normal.y)) {
        float invLen = 1.0 / sqrt(normal.x * normal.x + normal.z * normal.z);
        tangent = float3(-normal.z * invLen, 0, normal.x * invLen);
    } else {
        float invLen = 1.0 / sqrt(normal.y * normal.y + normal.z * normal.z);
        tangent = float3(0, normal.z * invLen, -normal.y * invLen);
    }
    bitangent = cross(normal, tangent);
}

// Transform from local to world space
float3 LocalToWorld(float3 v, float3 tangent, float3 bitangent, float3 normal) {
    return v.x * tangent + v.y * bitangent + v.z * normal;
}

#endif // RANDOM_HLSLI
