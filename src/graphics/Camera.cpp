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

void Camera::Update(float deltaTime, GLFWwindow* window, bool allowInput) {
    if (!window || !allowInput) {
        m_firstMouse = true;
        return;
    }

    // Check if right mouse button is pressed to rotate camera
    const bool isRmbHeld = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    if (m_firstMouse) {
        m_lastMouseX = mouseX;
        m_lastMouseY = mouseY;
        m_firstMouse = false;
    }

    if (isRmbHeld) {
        const float dx = static_cast<float>(mouseX - m_lastMouseX);
        const float dy = static_cast<float>(mouseY - m_lastMouseY);

        m_yaw += dx * mouseSensitivity;
        m_pitch -= dy * mouseSensitivity;

        constexpr float maxPitch = 1.55f; // ~89 degrees
        m_pitch = std::clamp(m_pitch, -maxPitch, maxPitch);
    }

    m_lastMouseX = mouseX;
    m_lastMouseY = mouseY;

    // Movement vectors
    const float cosPitch = std::cos(m_pitch);
    const float sinPitch = std::sin(m_pitch);
    const float cosYaw = std::cos(m_yaw);
    const float sinYaw = std::sin(m_yaw);

    const Vector3 forward{sinYaw * cosPitch, sinPitch, cosYaw * cosPitch};
    const Vector3 right{cosYaw, 0.0f, -sinYaw};
    const Vector3 up{0.0f, 1.0f, 0.0f};

    float speed = moveSpeed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        speed *= 2.5f;
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        m_position += forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        m_position -= forward * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        m_position += right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        m_position -= right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        m_position += up * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        m_position -= up * speed;
    }
}

} // namespace Engine
