#include "graphics/Camera.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

Camera::Camera(const Vector3& position, float yaw, float pitch)
    : m_position(position), m_yaw(yaw), m_pitch(pitch) {}

void Camera::SetFreezeCulling(bool freeze) noexcept {
    if (freeze && !m_freezeCulling) {
        m_frozenCullingViewProj = GetViewProjectionMatrix();
    }
    m_freezeCulling = freeze;
}

Matrix4x4 Camera::GetCullingViewProjection() const noexcept {
    if (m_freezeCulling) {
        return m_frozenCullingViewProj;
    }
    return GetViewProjectionMatrix();
}

Matrix4x4 Camera::GetViewMatrix() const noexcept {
    const float cosPitch = std::cos(m_pitch);
    const float sinPitch = std::sin(m_pitch);
    const float cosYaw = std::cos(m_yaw);
    const float sinYaw = std::sin(m_yaw);

    const Vector3 forward{sinYaw * cosPitch, sinPitch, cosYaw * cosPitch};
    return Matrix4x4::LookAt(m_position, m_position + forward, Vector3{0.0f, 1.0f, 0.0f});
}

Matrix4x4 Camera::GetProjectionMatrix() const noexcept {
    return Matrix4x4::PerspectiveVk(fovRadians, m_aspect, zNear, zFar);
}

Matrix4x4 Camera::GetViewProjectionMatrix() const noexcept {
    return GetProjectionMatrix() * GetViewMatrix();
}

void Camera::Rotate(float yawDelta, float pitchDelta) noexcept {
    m_yaw += yawDelta;
    m_pitch += pitchDelta;
    constexpr float maxPitch = 1.55f;
    m_pitch = std::clamp(m_pitch, -maxPitch, maxPitch);
}

Vector3 Camera::GetForwardDirection() const noexcept {
    const float cosPitch = std::cos(m_pitch);
    const float sinPitch = std::sin(m_pitch);
    const float cosYaw = std::cos(m_yaw);
    const float sinYaw = std::sin(m_yaw);
    return {sinYaw * cosPitch, sinPitch, cosYaw * cosPitch};
}

Vector3 Camera::GetRightDirection() const noexcept {
    return {std::cos(m_yaw), 0.0f, -std::sin(m_yaw)};
}

} // namespace Engine
