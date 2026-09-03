#pragma once

#include "graphics/culling/MathTypes.hpp"
#include <GLFW/glfw3.h>

namespace Engine {

class Camera {
public:
    Camera(const Vector3& position = {0.0f, 6.0f, -22.0f},
           float yaw = 0.0f,
           float pitch = -0.1f);

    void Update(float deltaTime, GLFWwindow* window, bool allowInput = true);
    void SetAspect(float aspect) noexcept { m_aspect = aspect; }

    [[nodiscard]] Matrix4x4 GetViewMatrix() const noexcept;
    [[nodiscard]] Matrix4x4 GetProjectionMatrix() const noexcept;
    [[nodiscard]] Matrix4x4 GetViewProjectionMatrix() const noexcept;

    // Freeze camera for culling inspection
    void SetFreezeCulling(bool freeze) noexcept;
    [[nodiscard]] bool IsCullingFrozen() const noexcept { return m_freezeCulling; }
    [[nodiscard]] Matrix4x4 GetCullingViewProjection() const noexcept;

    [[nodiscard]] Vector3 GetPosition() const noexcept { return m_position; }
    void SetPosition(const Vector3& pos) noexcept { m_position = pos; }

    float moveSpeed{15.0f};
    float mouseSensitivity{0.003f};
    float fovRadians{1.04719755f}; // 60 deg
    float zNear{0.2f};
    float zFar{500.0f};

private:
    Vector3 m_position;
    float m_yaw{0.0f};   // Radians
    float m_pitch{0.0f}; // Radians
    float m_aspect{16.0f / 9.0f};

    bool m_firstMouse{true};
    double m_lastMouseX{0.0};
    double m_lastMouseY{0.0};

    bool m_freezeCulling{false};
    Matrix4x4 m_frozenCullingViewProj{Matrix4x4::Identity()};
};

} // namespace Engine
