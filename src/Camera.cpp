#include "Camera.h"
#include <glm/gtc/constants.hpp>

namespace ACG {

Camera::Camera() 
    : m_position(0.0f, 0.0f, 5.0f)
    , m_direction(0.0f, 0.0f, -1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_fov(45.0f)
    , m_aspectRatio(16.0f / 9.0f)
    , m_aperture(0.0f)
    , m_focusDistance(5.0f)
{
}

Camera::~Camera() {
}

void Camera::SetPosition(const glm::vec3& position) {
    m_position = position;
}

void Camera::SetTarget(const glm::vec3& target) {
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

void Camera::GenerateRay(float u, float v, glm::vec3& origin, glm::vec3& direction) const {
    // TODO: Generate ray with depth of field
    origin = m_position;
    direction = m_direction;  // Simplified
}

void Camera::Rotate(float yaw, float pitch) {
    // TODO: Implement camera rotation
}

void Camera::Move(const glm::vec3& offset) {
    m_position += offset;
}

void Camera::UpdateVectors() {
    m_right = glm::normalize(glm::cross(m_direction, m_up));
    m_up = glm::normalize(glm::cross(m_right, m_direction));
}

} // namespace ACG
