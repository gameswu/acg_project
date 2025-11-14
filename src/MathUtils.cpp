#include "MathUtils.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace ACG {
namespace MathUtils {

void CreateCoordinateSystem(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent) {
    // Build orthonormal basis from normal
    if (std::abs(normal.x) > std::abs(normal.y)) {
        tangent = glm::vec3(-normal.z, 0.0f, normal.x) / std::sqrt(normal.x * normal.x + normal.z * normal.z);
    } else {
        tangent = glm::vec3(0.0f, normal.z, -normal.y) / std::sqrt(normal.y * normal.y + normal.z * normal.z);
    }
    bitangent = glm::cross(normal, tangent);
}

glm::vec3 LocalToWorld(const glm::vec3& v, const glm::vec3& tangent,
                       const glm::vec3& bitangent, const glm::vec3& normal) {
    return v.x * tangent + v.y * bitangent + v.z * normal;
}

glm::vec3 WorldToLocal(const glm::vec3& v, const glm::vec3& tangent,
                       const glm::vec3& bitangent, const glm::vec3& normal) {
    return glm::vec3(glm::dot(v, tangent), glm::dot(v, bitangent), glm::dot(v, normal));
}

glm::vec3 Reflect(const glm::vec3& incident, const glm::vec3& normal) {
    return incident - 2.0f * glm::dot(incident, normal) * normal;
}

glm::vec3 Refract(const glm::vec3& incident, const glm::vec3& normal, float eta) {
    float cosI = glm::dot(-incident, normal);
    float sinT2 = eta * eta * (1.0f - cosI * cosI);
    
    if (sinT2 > 1.0f) {
        // Total internal reflection
        return glm::vec3(0.0f);
    }
    
    float cosT = std::sqrt(1.0f - sinT2);
    return eta * incident + (eta * cosI - cosT) * normal;
}

float Fresnel(float cosTheta, float eta) {
    // Schlick's approximation
    float r0 = (1.0f - eta) / (1.0f + eta);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * std::pow(1.0f - cosTheta, 5.0f);
}

glm::vec3 FresnelSchlick(float cosTheta, const glm::vec3& F0) {
    return F0 + (glm::vec3(1.0f) - F0) * std::pow(1.0f - cosTheta, 5.0f);
}

glm::vec2 DirectionToSpherical(const glm::vec3& dir) {
    float theta = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
    float phi = std::atan2(dir.z, dir.x);
    if (phi < 0.0f) phi += 2.0f * PI;
    return glm::vec2(theta, phi);
}

glm::vec3 SphericalToDirection(float theta, float phi) {
    float sinTheta = std::sin(theta);
    return glm::vec3(
        sinTheta * std::cos(phi),
        std::cos(theta),
        sinTheta * std::sin(phi)
    );
}

glm::vec3 LinearToSRGB(const glm::vec3& color) {
    return glm::pow(color, glm::vec3(1.0f / 2.2f));
}

glm::vec3 SRGBToLinear(const glm::vec3& color) {
    return glm::pow(color, glm::vec3(2.2f));
}

float Luminance(const glm::vec3& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

float SafeSqrt(float x) {
    return std::sqrt(std::max(0.0f, x));
}

bool SolveQuadratic(float a, float b, float c, float& t0, float& t1) {
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) {
        return false;
    }
    
    float sqrtD = std::sqrt(discriminant);
    float q = (b < 0.0f) ? (-b - sqrtD) * 0.5f : (-b + sqrtD) * 0.5f;
    
    t0 = q / a;
    t1 = c / q;
    
    if (t0 > t1) {
        std::swap(t0, t1);
    }
    
    return true;
}

bool RayTriangleIntersect(const Ray& ray, const glm::vec3& v0,
                          const glm::vec3& v1, const glm::vec3& v2,
                          float& t, float& u, float& v) {
    // Möller-Trumbore algorithm
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(ray.direction, edge2);
    float a = glm::dot(edge1, h);
    
    if (std::abs(a) < EPSILON) {
        return false;  // Ray parallel to triangle
    }
    
    float f = 1.0f / a;
    glm::vec3 s = ray.origin - v0;
    u = f * glm::dot(s, h);
    
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    
    glm::vec3 q = glm::cross(s, edge1);
    v = f * glm::dot(ray.direction, q);
    
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    
    t = f * glm::dot(edge2, q);
    
    return t > ray.tMin && t < ray.tMax;
}

} // namespace MathUtils
} // namespace ACG
