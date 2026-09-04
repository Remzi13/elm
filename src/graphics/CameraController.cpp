#include "graphics/CameraController.hpp"

namespace elm {

	bool CameraController::OnKeyEvent(const KeyEvent& event) {
		const int key = static_cast<int>(event.key);
		if (key >= 0 && key < static_cast<int>(m_keys.size())) {
			if (event.action == KeyAction::Press || event.action == KeyAction::Repeat) {
				m_keys[static_cast<size_t>(key)] = true;
			}
			else if (event.action == KeyAction::Release) {
				m_keys[static_cast<size_t>(key)] = false;
			}
		}

		if (m_rightMouseDown) {
			if (event.key == Key::W || event.key == Key::A || event.key == Key::S ||
				event.key == Key::D || event.key == Key::Q || event.key == Key::E ||
				event.key == Key::LeftShift) {
				return true;
			}
		}
		return false;
	}

	bool CameraController::OnMouseButtonEvent(const MouseButtonEvent& event) {
		if (event.button == MouseButton::Right || static_cast<uint8_t>(event.button) == 1) {
			m_rightMouseDown = (event.action == KeyAction::Press);
			return true;
		}
		return false;
	}

	bool CameraController::OnMouseMoveEvent(const MouseMoveEvent& event) {
		if (!m_hasMousePosition) {
			m_lastMouseX = event.x;
			m_lastMouseY = event.y;
			m_hasMousePosition = true;
			return false;
		}

		bool handled = false;
		if (m_rightMouseDown) {
			m_camera.Rotate(
				static_cast<float>(event.x - m_lastMouseX) * m_camera.mouseSensitivity,
				-static_cast<float>(event.y - m_lastMouseY) * m_camera.mouseSensitivity);
			handled = true;
		}

		m_lastMouseX = event.x;
		m_lastMouseY = event.y;
		return handled;
	}

	void CameraController::Update(float deltaTime) {
		const float speedMultiplier = m_keys[static_cast<size_t>(Key::LeftShift)] ? 2.5f : 1.0f;
		const float speed = m_camera.moveSpeed * deltaTime * speedMultiplier;
		const Vector3 forward = m_camera.GetForwardDirection();
		const Vector3 right = m_camera.GetRightDirection();
		const Vector3 up{ 0.0f, 1.0f, 0.0f };

		Vector3 movement{ 0.0f, 0.0f, 0.0f };
		if (m_keys[static_cast<size_t>(Key::W)]) movement += forward;
		if (m_keys[static_cast<size_t>(Key::S)]) movement -= forward;
		if (m_keys[static_cast<size_t>(Key::D)]) movement += right;
		if (m_keys[static_cast<size_t>(Key::A)]) movement -= right;
		if (m_keys[static_cast<size_t>(Key::E)]) movement += up;
		if (m_keys[static_cast<size_t>(Key::Q)]) movement -= up;

		m_camera.Translate(movement * speed);
	}

	void CameraController::Update(float deltaTime, const Vector<InputEvent>& events) {
		for (const auto& event : events) {
			if (event.type == elm::InputEventType::Key) {
				OnKeyEvent(event.AsKeyEvent());
			}
			else if (event.type == elm::InputEventType::MouseButton) {
				OnMouseButtonEvent(event.AsMouseButtonEvent());
			}
			else if (event.type == elm::InputEventType::CursorMove) {
				OnMouseMoveEvent(event.AsMouseMoveEvent());
			}
		}
		Update(deltaTime);
	}

} // namespace elm
