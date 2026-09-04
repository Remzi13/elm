#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace elm {

enum class InputEventType : uint8_t {
	Key,
	Character,
	MouseButton,
	CursorMove,
	CursorEnter,
	Scroll
};

enum class InputAction {
	Release = 0,
	Press = 1,
	Repeat = 2
};

enum class InputKey {
	A = 65,
	D = 68,
	E = 69,
	Q = 81,
	S = 83,
	W = 87,
	LeftShift = 340
};

enum class InputMouseButton : int {
	Right = 1
};

struct InputEvent {
	InputEventType type{InputEventType::Key};
	InputKey key{-1};
	int scancode{0};
	InputAction action{0};
	int modifiers{0};
	int button{0};
	unsigned int codepoint{0};
	double x{0.0};
	double y{0.0};
};

class InputSystem {
public:
	InputSystem();
	~InputSystem();

	InputSystem(const InputSystem&) = delete;
	InputSystem& operator=(const InputSystem&) = delete;
	InputSystem(InputSystem&&) noexcept;
	InputSystem& operator=(InputSystem&&) noexcept;

	void AttachWindow(void* window);
	void BeginFrame() noexcept;
	void Update(bool allowInput = true);
	[[nodiscard]] const std::vector<InputEvent>& GetEvents() const noexcept { return m_events; }

private:
	std::vector<InputEvent> m_events;	
	std::unique_ptr<struct Impl> m_impl;
};
}