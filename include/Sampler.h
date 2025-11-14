#pragma once

#include <glm/glm.hpp>
#include <random>

namespace ACG {

/**
 * @brief Sampling utilities for path tracing
 * 
 * Implements various importance sampling techniques:
 * - Cosine-weighted hemisphere sampling
 * - BRDF/BSDF importance sampling
 * - Multiple importance sampling (MIS)
 * - Russian Roulette termination
 */
class Sampler {
public:
    Sampler();
    explicit Sampler(uint32_t seed);
    
    // Random number generation
    float Random();  // [0, 1)
    glm::vec2 Random2D();
    
    // Hemisphere sampling
    glm::vec3 SampleHemisphere(const glm::vec3& normal);
    glm::vec3 SampleCosineHemisphere(const glm::vec3& normal);
    
    // BRDF sampling
    glm::vec3 SampleDiffuse(const glm::vec3& normal, float& pdf);
    glm::vec3 SampleSpecular(const glm::vec3& reflected, float roughness, float& pdf);
    glm::vec3 SampleGGX(const glm::vec3& normal, const glm::vec3& view, float roughness, float& pdf);
    
    // Disk sampling (for depth of field)
    glm::vec2 SampleDisk(float radius);
    
    // Multiple Importance Sampling
    static float PowerHeuristic(float pdfA, float pdfB, int beta = 2);
    static float BalanceHeuristic(float pdfA, float pdfB);
    
    // Russian Roulette
    bool RussianRoulette(float survivalProbability);

private:
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist;
};

/**
 * @brief Helper functions for sampling
 */
namespace SamplingUtils {
    // Convert uniform samples to various distributions
    glm::vec3 UniformSampleHemisphere(float u1, float u2);
    glm::vec3 CosineSampleHemisphere(float u1, float u2);
    glm::vec2 UniformSampleDisk(float u1, float u2);
    glm::vec2 ConcentricSampleDisk(float u1, float u2);
    
    // PDF calculations
    float UniformHemispherePDF();
    float CosineHemispherePDF(float cosTheta);
    float GGX_PDF(float cosTheta, float roughness);
    
    // Microfacet distribution functions
    float GGX_D(float cosTheta, float roughness);
    float GGX_G(float cosTheta, float roughness);
    glm::vec3 GGX_Sample(float u1, float u2, float roughness);
}

} // namespace ACG
