#include "Sampler.h"
#include <glm/gtc/constants.hpp>

namespace ACG {

Sampler::Sampler()
    : m_rng(std::random_device{}())
    , m_dist(0.0f, 1.0f)
{
}

Sampler::Sampler(uint32_t seed)
    : m_rng(seed)
    , m_dist(0.0f, 1.0f)
{
}

float Sampler::Random() {
    return m_dist(m_rng);
}

glm::vec2 Sampler::Random2D() {
    return glm::vec2(Random(), Random());
}

glm::vec3 Sampler::SampleHemisphere(const glm::vec3& normal) {
    glm::vec2 u = Random2D();
    glm::vec3 dir = SamplingUtils::UniformSampleHemisphere(u.x, u.y);
    // TODO: Transform to world space with normal as up vector
    return dir;
}

glm::vec3 Sampler::SampleCosineHemisphere(const glm::vec3& normal) {
    glm::vec2 u = Random2D();
    glm::vec3 dir = SamplingUtils::CosineSampleHemisphere(u.x, u.y);
    // TODO: Transform to world space
    return dir;
}

glm::vec3 Sampler::SampleDiffuse(const glm::vec3& normal, float& pdf) {
    glm::vec3 dir = SampleCosineHemisphere(normal);
    float cosTheta = glm::dot(dir, normal);
    pdf = SamplingUtils::CosineHemispherePDF(cosTheta);
    return dir;
}

glm::vec3 Sampler::SampleSpecular(const glm::vec3& reflected, float roughness, float& pdf) {
    // TODO: Sample around reflected direction with roughness
    pdf = 1.0f;
    return reflected;
}

glm::vec3 Sampler::SampleGGX(const glm::vec3& normal, const glm::vec3& view, float roughness, float& pdf) {
    // TODO: Implement GGX importance sampling
    glm::vec2 u = Random2D();
    glm::vec3 h = SamplingUtils::GGX_Sample(u.x, u.y, roughness);
    // Transform to world space and compute PDF
    pdf = 1.0f;
    return h;
}

glm::vec2 Sampler::SampleDisk(float radius) {
    glm::vec2 u = Random2D();
    return SamplingUtils::ConcentricSampleDisk(u.x, u.y) * radius;
}

float Sampler::PowerHeuristic(float pdfA, float pdfB, int beta) {
    float a = std::pow(pdfA, beta);
    float b = std::pow(pdfB, beta);
    return a / (a + b);
}

float Sampler::BalanceHeuristic(float pdfA, float pdfB) {
    return pdfA / (pdfA + pdfB);
}

bool Sampler::RussianRoulette(float survivalProbability) {
    return Random() < survivalProbability;
}

// SamplingUtils implementation

namespace SamplingUtils {

glm::vec3 UniformSampleHemisphere(float u1, float u2) {
    // TODO: Implement uniform hemisphere sampling
    float phi = 2.0f * glm::pi<float>() * u1;
    float cosTheta = u2;
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    
    return glm::vec3(
        std::cos(phi) * sinTheta,
        std::sin(phi) * sinTheta,
        cosTheta
    );
}

glm::vec3 CosineSampleHemisphere(float u1, float u2) {
    // TODO: Implement cosine-weighted hemisphere sampling
    glm::vec2 disk = ConcentricSampleDisk(u1, u2);
    float z = std::sqrt(std::max(0.0f, 1.0f - disk.x * disk.x - disk.y * disk.y));
    return glm::vec3(disk.x, disk.y, z);
}

glm::vec2 UniformSampleDisk(float u1, float u2) {
    float r = std::sqrt(u1);
    float theta = 2.0f * glm::pi<float>() * u2;
    return glm::vec2(r * std::cos(theta), r * std::sin(theta));
}

glm::vec2 ConcentricSampleDisk(float u1, float u2) {
    // Map [0,1]^2 to [-1,1]^2
    float a = 2.0f * u1 - 1.0f;
    float b = 2.0f * u2 - 1.0f;
    
    if (a == 0.0f && b == 0.0f) {
        return glm::vec2(0.0f);
    }
    
    // TODO: Implement concentric disk sampling (Shirley mapping)
    float r, theta;
    if (std::abs(a) > std::abs(b)) {
        r = a;
        theta = (glm::pi<float>() / 4.0f) * (b / a);
    } else {
        r = b;
        theta = (glm::pi<float>() / 2.0f) - (glm::pi<float>() / 4.0f) * (a / b);
    }
    
    return glm::vec2(r * std::cos(theta), r * std::sin(theta));
}

float UniformHemispherePDF() {
    return 1.0f / (2.0f * glm::pi<float>());
}

float CosineHemispherePDF(float cosTheta) {
    return cosTheta / glm::pi<float>();
}

float GGX_PDF(float cosTheta, float roughness) {
    // TODO: Calculate GGX PDF
    return 0.0f;
}

float GGX_D(float cosTheta, float roughness) {
    // TODO: GGX normal distribution function
    return 0.0f;
}

float GGX_G(float cosTheta, float roughness) {
    // TODO: GGX geometry function
    return 0.0f;
}

glm::vec3 GGX_Sample(float u1, float u2, float roughness) {
    // TODO: Importance sample GGX distribution
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

} // namespace SamplingUtils

} // namespace ACG
