#pragma once

#include <glm/glm.hpp>

namespace ACG {

/**
 * @brief Camera for view and projection transformations
 */
class Camera {
public:
    Camera();
    ~Camera();

    // 设置相机参数
    void SetPosition(const glm::vec3& position);
    void SetTarget(const glm::vec3& target);
    void SetUp(const glm::vec3& up);
    void SetFOV(float fov);
    void SetAspectRatio(float aspect);
    void SetAperture(float aperture);  // For depth of field
    void SetFocusDistance(float distance);  // For depth of field
    
    // 获取相机参数
    glm::vec3 GetPosition() const { return m_position; }
    glm::vec3 GetDirection() const { return m_direction; }
    glm::vec3 GetRight() const { return m_right; }
    glm::vec3 GetUp() const { return m_up; }
    float GetFOV() const { return m_fov; }
    float GetAperture() const { return m_aperture; }
    float GetFocusDistance() const { return m_focusDistance; }
    
    // 生成光线（用于路径追踪）
    void GenerateRay(float u, float v, glm::vec3& origin, glm::vec3& direction) const;
    
    // 相机控制
    void Rotate(float yaw, float pitch);
    void Move(const glm::vec3& offset);

private:
    glm::vec3 m_position;
    glm::vec3 m_direction;
    glm::vec3 m_right;
    glm::vec3 m_up;
    
    float m_fov;
    float m_aspectRatio;
    float m_aperture;       // Depth of field
    float m_focusDistance;  // Depth of field
    
    void UpdateVectors();
};

} // namespace ACG
