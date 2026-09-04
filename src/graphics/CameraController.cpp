#include "graphics/CameraController.hpp"

namespace Engine {

void CameraController::Update(float deltaTime, const std::vector<elm::InputEvent>& events) {
    for (const auto& event : events) {
        if (event.type == elm::InputEventType::Key && event.key >= 0 && event.key < static_cast<int>(m_keys.size())) {
            if (event.action == static_cast<int>(elm::InputAction::Press) ||
                event.action == static_cast<int>(elm::InputAction::Repeat)) {
                m_keys[static_cast<size_t>(event.key)] = true;
            } else if (event.action == static_cast<int>(elm::InputAction::Release)) {
                m_keys[static_cast<size_t>(event.key)] = false;
            }
        } else if (event.type == elm::InputEventType::MouseButton &&
                   event.button == static_cast<int>(elm::InputMouseButton::Right)) {
            m_rightMouseDown = event.action == static_cast<int>(elm::InputAction::Press);
        } else if (event.type == elm::InputEventType::CursorMove) {
            if (!m_hasMousePosition) {
                m_lastMouseX = event.x;
                m_lastMouseY = event.y;
                m_hasMousePosition = true;
            } else if (m_rightMouseDown) {
                m_camera.Rotate(
                    static_cast<float>(event.x - m_lastMouseX) * m_camera.mouseSensitivity,
                    -static_cast<float>(event.y - m_lastMouseY) * m_camera.mouseSensitivity);
            }
            m_lastMouseX = event.x;
            m_lastMouseY = event.y;
        }
    }

    const float speedMultiplier = m_keys[static_cast<int>(elm::InputKey::LeftShift)] ? 2.5f : 1.0f;
    const float speed = m_camera.moveSpeed * deltaTime * speedMultiplier;
    const Vector3 forward = m_camera.GetForwardDirection();
    const Vector3 right = m_camera.GetRightDirection();
    const Vector3 up{0.0f, 1.0f, 0.0f};

    Vector3 movement{0.0f, 0.0f, 0.0f};
    if (m_keys[static_cast<int>(elm::InputKey::W)]) movement += forward;
    if (m_keys[static_cast<int>(elm::InputKey::S)]) movement -= forward;
    if (m_keys[static_cast<int>(elm::InputKey::D)]) movement += right;
    if (m_keys[static_cast<int>(elm::InputKey::A)]) movement -= right;
    if (m_keys[static_cast<int>(elm::InputKey::E)]) movement += up;
    if (m_keys[static_cast<int>(elm::InputKey::Q)]) movement -= up;

    m_camera.Translate(movement * speed);
}

} // namespace Engine
