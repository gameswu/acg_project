#pragma once

#include <glm/glm.hpp>

namespace ACG {

/**
 * @brief Ray structure for ray tracing
 */
struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
    float tMin;
    float tMax;
    
    Ray() : origin(0.0f), direction(0.0f, 0.0f, 1.0f), tMin(0.001f), tMax(1e30f) {}
    Ray(const glm::vec3& o, const glm::vec3& d) 
        : origin(o), direction(d), tMin(0.001f), tMax(1e30f) {}
    
    glm::vec3 At(float t) const { return origin + direction * t; }
};

/**
 * @brief Hit information for ray-surface intersection
 */
struct HitInfo {
    bool hit;
    float t;
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    int materialIndex;
    int triangleIndex;
    
    HitInfo() : hit(false), t(0.0f), materialIndex(-1), triangleIndex(-1) {}
};

/**
 * @brief Math utility functions
 */
namespace MathUtils {
    // Constants
    constexpr float PI = 3.14159265358979323846f;
    constexpr float INV_PI = 0.31830988618379067154f;
    constexpr float EPSILON = 1e-6f;
    
    // Coordinate system construction
    void CreateCoordinateSystem(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent);
    
    // Transform between local and world space
    glm::vec3 LocalToWorld(const glm::vec3& v, const glm::vec3& tangent, 
                           const glm::vec3& bitangent, const glm::vec3& normal);
    glm::vec3 WorldToLocal(const glm::vec3& v, const glm::vec3& tangent,
                           const glm::vec3& bitangent, const glm::vec3& normal);
    
    // Reflection and refraction
    glm::vec3 Reflect(const glm::vec3& incident, const glm::vec3& normal);
    glm::vec3 Refract(const glm::vec3& incident, const glm::vec3& normal, float eta);
    float Fresnel(float cosTheta, float eta);
    glm::vec3 FresnelSchlick(float cosTheta, const glm::vec3& F0);
    
    // Spherical coordinates
    glm::vec2 DirectionToSpherical(const glm::vec3& dir);
    glm::vec3 SphericalToDirection(float theta, float phi);
    
    // Color utilities
    glm::vec3 LinearToSRGB(const glm::vec3& color);
    glm::vec3 SRGBToLinear(const glm::vec3& color);
    float Luminance(const glm::vec3& color);
    
    // Utility functions
    float SafeSqrt(float x);
    bool SolveQuadratic(float a, float b, float c, float& t0, float& t1);
    
    // Ray-triangle intersection
    bool RayTriangleIntersect(const Ray& ray, const glm::vec3& v0, 
                              const glm::vec3& v1, const glm::vec3& v2,
                              float& t, float& u, float& v);
}

} // namespace ACG
