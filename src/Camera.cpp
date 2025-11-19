#include "Camera.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ACG {

Camera::Camera() 
    : m_position(0.0f, 1.0f, 3.0f)
    , m_target(0.0f, 1.0f, 0.0f)  // Look toward center of scene
    , m_direction(0.0f, 0.0f, -1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_fov(60.0f)
    , m_aspectRatio(16.0f / 9.0f)
    , m_aperture(0.0f)
    , m_focusDistance(5.0f)
{
    SetTarget(m_target);
}

Camera::~Camera() {
}

void Camera::SetPosition(const glm::vec3& position) {
    m_position = position;
}

void Camera::SetTarget(const glm::vec3& target) {
    m_target = target;
    m_direction = glm::normalize(target - m_position);
    UpdateVectors();
}

void Camera::SetUp(const glm::vec3& up) {
    m_up = glm::normalize(up);
    UpdateVectors();
}

void Camera::SetFOV(float fov) {
    m_fov = fov;
}

void Camera::SetAspectRatio(float aspect) {
    m_aspectRatio = aspect;
}

void Camera::SetAperture(float aperture) {
    m_aperture = aperture;
}

void Camera::SetFocusDistance(float distance) {
    m_focusDistance = distance;
}

glm::mat4 Camera::GetProjectionMatrix() const {
    return glm::perspective(glm::radians(m_fov), m_aspectRatio, 0.1f, 100.0f);
}

void Camera::GenerateRay(float u, float v, glm::vec3& origin, glm::vec3& direction) const {
    // Calculate viewport dimensions at focus distance
    float theta = glm::radians(m_fov);
    float h = std::tan(theta / 2.0f);
    float viewportHeight = 2.0f * h * m_focusDistance;
    float viewportWidth = viewportHeight * m_aspectRatio;
    
    // Calculate basis vectors
    glm::vec3 w = -m_direction;  // Camera looks in -z
    glm::vec3 u_vec = m_right;
    glm::vec3 v_vec = m_up;
    
    // Calculate viewport corner
    glm::vec3 horizontal = viewportWidth * u_vec;
    glm::vec3 vertical = viewportHeight * v_vec;
    glm::vec3 lowerLeftCorner = m_position - horizontal/2.0f - vertical/2.0f + m_focusDistance * m_direction;
    
    // Calculate ray direction
    glm::vec3 target = lowerLeftCorner + u * horizontal + v * vertical;
    
    // Apply depth of field if aperture > 0
    if (m_aperture > 0.0f) {
        // Random point on lens (would need random numbers in practice)
        // For now, use center of lens (no DOF effect in placeholder)
        origin = m_position;
        direction = glm::normalize(target - origin);
    } else {
        origin = m_position;
        direction = glm::normalize(target - origin);
    }
}

void Camera::Rotate(float yaw, float pitch) {
    // Yaw rotation (around world up)
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::mat3 yawRotation = glm::mat3(
        std::cos(yaw), 0.0f, std::sin(yaw),
        0.0f, 1.0f, 0.0f,
        -std::sin(yaw), 0.0f, std::cos(yaw)
    );
    m_direction = glm::normalize(yawRotation * m_direction);
    
    // Pitch rotation (around right vector)
    // Clamp pitch to avoid gimbal lock
    float currentPitch = std::asin(-m_direction.y);
    float newPitch = glm::clamp(currentPitch + pitch, -glm::pi<float>() * 0.49f, glm::pi<float>() * 0.49f);
    pitch = newPitch - currentPitch;
    
    glm::mat3 pitchRotation = glm::mat3(
        std::cos(pitch) + m_right.x * m_right.x * (1 - std::cos(pitch)),
        m_right.x * m_right.y * (1 - std::cos(pitch)) - m_right.z * std::sin(pitch),
        m_right.x * m_right.z * (1 - std::cos(pitch)) + m_right.y * std::sin(pitch),
        
        m_right.y * m_right.x * (1 - std::cos(pitch)) + m_right.z * std::sin(pitch),
        std::cos(pitch) + m_right.y * m_right.y * (1 - std::cos(pitch)),
        m_right.y * m_right.z * (1 - std::cos(pitch)) - m_right.x * std::sin(pitch),
        
        m_right.z * m_right.x * (1 - std::cos(pitch)) - m_right.y * std::sin(pitch),
        m_right.z * m_right.y * (1 - std::cos(pitch)) + m_right.x * std::sin(pitch),
        std::cos(pitch) + m_right.z * m_right.z * (1 - std::cos(pitch))
    );
    m_direction = glm::normalize(pitchRotation * m_direction);
    
    UpdateVectors();
}

void Camera::Move(const glm::vec3& offset) {
    m_position += offset;
}

void Camera::UpdateVectors() {
    // Preserve user-specified up vector when possible to allow roll control
    glm::vec3 desiredUp = glm::length(m_up) > 0.0f ? glm::normalize(m_up) : glm::vec3(0.0f, 1.0f, 0.0f);

    // If desired up is nearly parallel to direction, choose an alternate basis to avoid degeneracy
    if (glm::abs(glm::dot(desiredUp, m_direction)) > 0.999f) {
        desiredUp = std::abs(m_direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 right = glm::cross(m_direction, desiredUp);
    float rightLen = glm::length(right);
    if (rightLen < 1e-6f) {
        // Fallback if direction and desired up were still nearly parallel
        desiredUp = glm::vec3(0.0f, 1.0f, 0.0f);
        right = glm::cross(m_direction, desiredUp);
        rightLen = glm::length(right);
    }
    m_right = glm::normalize(right);
    m_up = glm::normalize(glm::cross(m_right, m_direction));
}

} // namespace ACG
