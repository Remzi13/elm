#include "input/Input.hpp"

#include <GLFW/glfw3.h>
#include <utility>

namespace elm {

	struct InputSystem::Impl {
		GLFWwindow* window{ nullptr };
		void* previousUserPointer{ nullptr };
		std::vector<InputEvent>* events{ nullptr };

		void Detach() {
			if (window && glfwGetWindowUserPointer(window) == this) {
				glfwSetWindowUserPointer(window, previousUserPointer);
			}
			window = nullptr;
			previousUserPointer = nullptr;
		}

		static Impl* FromWindow(GLFWwindow* window) {
			return static_cast<Impl*>(glfwGetWindowUserPointer(window));
		}

		static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
			if (auto* impl = FromWindow(window)) {
				impl->events->push_back({
					InputEventType::Key,
					static_cast<InputKey>(key),
					scancode,
					static_cast<InputAction>(action),
					mods
				});
			}
		}

		static void CharacterCallback(GLFWwindow* window, unsigned int codepoint) {
			if (auto* impl = FromWindow(window)) {
				InputEvent event;
				event.type = InputEventType::Character;
				event.codepoint = codepoint;
				impl->events->push_back(event);
			}
		}

		static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
			if (auto* impl = FromWindow(window)) {
				InputEvent event;
				event.type = InputEventType::MouseButton;
				event.button = button;
				event.action = static_cast<InputAction>(action);
				event.modifiers = mods;
				impl->events->push_back(event);
			}
		}

		static void CursorPositionCallback(GLFWwindow* window, double x, double y) {
			if (auto* impl = FromWindow(window)) {
				InputEvent event;
				event.type = InputEventType::CursorMove;
				event.x = x;
				event.y = y;
				impl->events->push_back(event);
			}
		}

		static void CursorEnterCallback(GLFWwindow* window, int entered) {
			if (auto* impl = FromWindow(window)) {
				InputEvent event;
				event.type = InputEventType::CursorEnter;
				event.action = static_cast<InputAction>(entered);
				impl->events->push_back(event);
			}
		}

		static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
			if (auto* impl = FromWindow(window)) {
				InputEvent event;
				event.type = InputEventType::Scroll;
				event.x = xOffset;
				event.y = yOffset;
				impl->events->push_back(event);
			}
		}
	};

	InputSystem::InputSystem() : m_impl(std::make_unique<Impl>()) {
		m_impl->events = &m_events;
	}

	InputSystem::~InputSystem() {
		if (m_impl) m_impl->Detach();
	}

	InputSystem::InputSystem(InputSystem&& other) noexcept
		: m_events(std::move(other.m_events)), m_impl(std::move(other.m_impl)) {
		if (m_impl) m_impl->events = &m_events;
	}

	InputSystem& InputSystem::operator=(InputSystem&& other) noexcept {
		if (this == &other) return *this;
		if (m_impl) m_impl->Detach();
		m_events = std::move(other.m_events);
		m_impl = std::move(other.m_impl);
		if (m_impl) m_impl->events = &m_events;
		return *this;
	}

	void InputSystem::AttachWindow(void* nativeWindow) {
		auto* window = static_cast<GLFWwindow*>(nativeWindow);
		if (m_impl->window == window) return;

		m_impl->Detach();
		m_impl->window = window;
		if (!window) return;

		m_impl->previousUserPointer = glfwGetWindowUserPointer(window);
		glfwSetWindowUserPointer(window, m_impl.get());
		glfwSetKeyCallback(window, Impl::KeyCallback);
		glfwSetCharCallback(window, Impl::CharacterCallback);
		glfwSetMouseButtonCallback(window, Impl::MouseButtonCallback);
		glfwSetCursorPosCallback(window, Impl::CursorPositionCallback);
		glfwSetCursorEnterCallback(window, Impl::CursorEnterCallback);
		glfwSetScrollCallback(window, Impl::ScrollCallback);
	}

	void InputSystem::BeginFrame() noexcept {
		m_events.clear();
	}

	void InputSystem::Update(bool allowInput) {
		if (!allowInput) m_events.clear();
	}

} // namespace elm