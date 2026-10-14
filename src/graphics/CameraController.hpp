#pragma once

#include "graphics/Camera.hpp"
#include "input/Input.hpp"

#include <array>

namespace elm {

	class CameraController : public IInputSubscriber {
	public:
		explicit CameraController(Camera& camera) noexcept : m_camera(camera) {}

		void Update(float deltaTime);
		void Update(float deltaTime, const Vector<InputEvent>& events);

		// Input is accepted only while the scene viewport is hovered (mouse) or focused (keyboard)
		void SetViewportState(bool hovered, bool focused) noexcept;

		// IInputSubscriber interface
		bool OnKeyEvent(const KeyEvent& event) override;
		bool OnMouseButtonEvent(const MouseButtonEvent& event) override;
		bool OnMouseMoveEvent(const MouseMoveEvent& event) override;

	private:
		Camera& m_camera;
		std::array<bool, 512> m_keys{};
		bool m_rightMouseDown{ false };
		bool m_viewportHovered{ false };
		bool m_viewportFocused{ false };
		bool m_hasMousePosition{ false };
		double m_lastMouseX{ 0.0 };
		double m_lastMouseY{ 0.0 };
	};

} // namespace elm
