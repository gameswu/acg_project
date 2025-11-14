#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <memory>

namespace ACG {

enum class LightType {
    Point,
    Directional,
    Area,
    Environment  // HDR environment map
};

/**
 * @brief Base light class
 */
class Light {
public:
    Light(LightType type);
    virtual ~Light();

    LightType GetType() const { return m_type; }
    
    void SetColor(const glm::vec3& color) { m_color = color; }
    void SetIntensity(float intensity) { m_intensity = intensity; }
    
    glm::vec3 GetColor() const { return m_color; }
    float GetIntensity() const { return m_intensity; }
    
    // Sample light (for direct lighting)
    virtual glm::vec3 Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const = 0;
    virtual float PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const = 0;

protected:
    LightType m_type;
    glm::vec3 m_color;
    float m_intensity;
};

/**
 * @brief Point light
 */
class PointLight : public Light {
public:
    PointLight();
    
    void SetPosition(const glm::vec3& position) { m_position = position; }
    glm::vec3 GetPosition() const { return m_position; }
    
    virtual glm::vec3 Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const override;
    virtual float PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const override;

private:
    glm::vec3 m_position;
};

/**
 * @brief Area light (rectangular)
 */
class AreaLight : public Light {
public:
    AreaLight();
    
    void SetPosition(const glm::vec3& position) { m_position = position; }
    void SetNormal(const glm::vec3& normal) { m_normal = normal; }
    void SetSize(const glm::vec2& size) { m_size = size; }
    
    virtual glm::vec3 Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const override;
    virtual float PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const override;

private:
    glm::vec3 m_position;
    glm::vec3 m_normal;
    glm::vec2 m_size;
};

/**
 * @brief Environment light with HDR skybox
 */
class EnvironmentLight : public Light {
public:
    EnvironmentLight();
    
    // Load HDR environment map
    bool LoadHDR(const std::string& filename);
    
    // Sample environment
    virtual glm::vec3 Sample(const glm::vec3& hitPoint, glm::vec3& lightDir, float& distance, float& pdf) const override;
    virtual float PDF(const glm::vec3& hitPoint, const glm::vec3& lightDir) const override;
    
    // Evaluate environment
    glm::vec3 Evaluate(const glm::vec3& direction) const;

private:
    // HDR environment map data
    int m_width;
    int m_height;
    std::vector<glm::vec3> m_data;
    
    // Importance sampling data structures
    std::vector<float> m_cdf;  // Cumulative distribution function
    
    void BuildCDF();
};

} // namespace ACG
