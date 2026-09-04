#include "input/Input.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>

namespace elm {

	// Implementation of ScopedInputSubscription
	ScopedInputSubscription::~ScopedInputSubscription() {
		Reset();
	}

	void ScopedInputSubscription::Reset() {
		if (m_system && m_id != 0) {
			m_system->RemoveSubscriber(m_id);
			m_system = nullptr;
			m_id = 0;
		}
	}

	namespace {
		static std::atomic<SubscriptionId> s_nextSubscriptionId{ 1 };
	}

	struct RegisteredSubscriber {
		SubscriptionId id{ 0 };
		int32_t priority{ 0 };
		String name;
		SharedPtr<IInputSubscriber> sharedSubscriber;
		IInputSubscriber* rawSubscriber{ nullptr };
		EventCallback callback;

		bool Dispatch(InputEvent& event) {
			if (callback) {
				return callback(event);
			}
			if (sharedSubscriber) {
				return sharedSubscriber->OnInputEvent(event);
			}
			if (rawSubscriber) {
				return rawSubscriber->OnInputEvent(event);
			}
			return false;
		}
	};

	struct InputSystemImpl {
		GLFWwindow* window{ nullptr };
		void* previousUserPointer{ nullptr };

		Vector<InputEvent>* rawEvents{ nullptr };
		Vector<RegisteredSubscriber> subscribers;

		// Immediate state tables
		std::array<bool, 512> currentKeys{};
		std::array<bool, 512> previousKeys{};
		std::array<bool, 32> currentMouseButtons{};
		std::array<bool, 32> previousMouseButtons{};

		double mouseX{ 0.0 };
		double mouseY{ 0.0 };
		double mouseDeltaX{ 0.0 };
		double mouseDeltaY{ 0.0 };
		double scrollDeltaX{ 0.0 };
		double scrollDeltaY{ 0.0 };
		bool hasInitialMousePos{ false };

		// Action & Axis contexts
		SharedPtr<InputContext> defaultContext{ MakeShared<InputContext>("Default") };
		Vector<SharedPtr<InputContext>> contextStack;

		void Detach() {
			if (window && glfwGetWindowUserPointer(window) == this) {
				glfwSetWindowUserPointer(window, previousUserPointer);
			}
			window = nullptr;
			previousUserPointer = nullptr;
		}

		static InputSystemImpl* FromWindow(GLFWwindow* win) {
			return static_cast<InputSystemImpl*>(glfwGetWindowUserPointer(win));
		}

		static void KeyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			if (key >= 0 && static_cast<size_t>(key) < impl->currentKeys.size()) {
				if (action == GLFW_PRESS) {
					impl->currentKeys[static_cast<size_t>(key)] = true;
				}
				else if (action == GLFW_RELEASE) {
					impl->currentKeys[static_cast<size_t>(key)] = false;
				}
			}

			InputEvent event;
			event.type = InputEventType::Key;
			event.key = static_cast<Key>(key);
			event.scancode = scancode;
			event.action = static_cast<KeyAction>(action);
			event.modifiers = mods;
			impl->rawEvents->push_back(event);
		}

		static void CharacterCallback(GLFWwindow* win, unsigned int codepoint) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			InputEvent event;
			event.type = InputEventType::Character;
			event.codepoint = codepoint;
			impl->rawEvents->push_back(event);
		}

		static void MouseButtonCallback(GLFWwindow* win, int button, int action, int mods) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			if (button >= 0 && static_cast<size_t>(button) < impl->currentMouseButtons.size()) {
				if (action == GLFW_PRESS) {
					impl->currentMouseButtons[static_cast<size_t>(button)] = true;
				}
				else if (action == GLFW_RELEASE) {
					impl->currentMouseButtons[static_cast<size_t>(button)] = false;
				}
			}

			InputEvent event;
			event.type = InputEventType::MouseButton;
			event.button = button;
			event.action = static_cast<KeyAction>(action);
			event.modifiers = mods;
			event.x = impl->mouseX;
			event.y = impl->mouseY;
			impl->rawEvents->push_back(event);
		}

		static void CursorPositionCallback(GLFWwindow* win, double x, double y) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			double dx = 0.0;
			double dy = 0.0;
			if (!impl->hasInitialMousePos) {
				impl->hasInitialMousePos = true;
			}
			else {
				dx = x - impl->mouseX;
				dy = y - impl->mouseY;
			}

			impl->mouseX = x;
			impl->mouseY = y;
			impl->mouseDeltaX += dx;
			impl->mouseDeltaY += dy;

			InputEvent event;
			event.type = InputEventType::CursorMove;
			event.x = x;
			event.y = y;
			event.deltaX = dx;
			event.deltaY = dy;
			impl->rawEvents->push_back(event);
		}

		static void CursorEnterCallback(GLFWwindow* win, int entered) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			InputEvent event;
			event.type = InputEventType::CursorEnter;
			event.action = entered ? InputAction::Press : InputAction::Release;
			impl->rawEvents->push_back(event);
		}

		static void ScrollCallback(GLFWwindow* win, double xOffset, double yOffset) {
			auto* impl = FromWindow(win);
			if (!impl || !impl->rawEvents) return;

			impl->scrollDeltaX += xOffset;
			impl->scrollDeltaY += yOffset;

			InputEvent event;
			event.type = InputEventType::Scroll;
			event.x = xOffset;
			event.y = yOffset;
			impl->rawEvents->push_back(event);
		}

		void SortSubscribers() {
			std::stable_sort(subscribers.begin(), subscribers.end(),
				[](const RegisteredSubscriber& a, const RegisteredSubscriber& b) noexcept {
					return a.priority > b.priority;
				});
		}
	};

	InputSystem::InputSystem() : m_impl(MakeUnique<InputSystemImpl>()) {
		m_impl->rawEvents = &m_rawEvents;
	}

	InputSystem::~InputSystem() {
		if (m_impl) {
			m_impl->Detach();
		}
	}

	InputSystem::InputSystem(InputSystem&& other) noexcept
		: m_rawEvents(std::move(other.m_rawEvents)),
		m_allEvents(std::move(other.m_allEvents)),
		m_unhandledEvents(std::move(other.m_unhandledEvents)),
		m_impl(std::move(other.m_impl)) {
		if (m_impl) {
			m_impl->rawEvents = &m_rawEvents;
		}
	}

	InputSystem& InputSystem::operator=(InputSystem&& other) noexcept {
		if (this != &other) {
			if (m_impl) m_impl->Detach();
			m_rawEvents = std::move(other.m_rawEvents);
			m_allEvents = std::move(other.m_allEvents);
			m_unhandledEvents = std::move(other.m_unhandledEvents);
			m_impl = std::move(other.m_impl);
			if (m_impl) {
				m_impl->rawEvents = &m_rawEvents;
			}
		}
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
		glfwSetKeyCallback(window, InputSystemImpl::KeyCallback);
		glfwSetCharCallback(window, InputSystemImpl::CharacterCallback);
		glfwSetMouseButtonCallback(window, InputSystemImpl::MouseButtonCallback);
		glfwSetCursorPosCallback(window, InputSystemImpl::CursorPositionCallback);
		glfwSetCursorEnterCallback(window, InputSystemImpl::CursorEnterCallback);
		glfwSetScrollCallback(window, InputSystemImpl::ScrollCallback);

		double currentX = 0.0;
		double currentY = 0.0;
		glfwGetCursorPos(window, &currentX, &currentY);
		m_impl->mouseX = currentX;
		m_impl->mouseY = currentY;
		m_impl->hasInitialMousePos = true;
	}

	void* InputSystem::GetAttachedWindow() const noexcept {
		return m_impl ? m_impl->window : nullptr;
	}

	void InputSystem::BeginFrame() noexcept {
		if (m_impl) {
			m_impl->previousKeys = m_impl->currentKeys;
			m_impl->previousMouseButtons = m_impl->currentMouseButtons;
			m_impl->mouseDeltaX = 0.0;
			m_impl->mouseDeltaY = 0.0;
			m_impl->scrollDeltaX = 0.0;
			m_impl->scrollDeltaY = 0.0;
		}
		m_rawEvents.clear();
		m_allEvents.clear();
		m_unhandledEvents.clear();
	}

	bool InputSystem::DispatchEvent(InputEvent& event) {
		if (!m_impl) return false;

		for (auto& subscriber : m_impl->subscribers) {
			if (subscriber.Dispatch(event)) {
				event.Consume();
				return true; // Stop propagation! Consumed!
			}
			if (event.IsConsumed()) {
				return true; // Explicitly marked consumed
			}
		}
		return false;
	}

	void InputSystem::Update(bool allowInput) {
		if (!allowInput || !m_impl) {
			m_rawEvents.clear();
			return;
		}

		m_allEvents.reserve(m_rawEvents.size());
		m_unhandledEvents.reserve(m_rawEvents.size());

		for (auto& event : m_rawEvents) {
			const bool consumed = DispatchEvent(event);

			if (!consumed && !event.IsConsumed()) {
				// Trigger action bindings in active contexts
				auto triggerActions = [&](const InputContext& context) {
					if (!context.IsActive()) return;

					if (event.type == InputEventType::Key) {
						for (const auto& [name, binding] : context.GetActions()) {
							for (const auto& mapping : binding.keyMappings) {
								if (mapping.key == event.key && mapping.triggerAction == event.action) {
									for (const auto& cb : binding.callbacks) {
										if (cb) cb();
									}
								}
							}
						}
					}
					else if (event.type == InputEventType::MouseButton) {
						for (const auto& [name, binding] : context.GetActions()) {
							for (const auto& mapping : binding.mouseMappings) {
								if (static_cast<int>(mapping.button) == event.button && mapping.triggerAction == event.action) {
									for (const auto& cb : binding.callbacks) {
										if (cb) cb();
									}
								}
							}
						}
					}
					};

				for (auto it = m_impl->contextStack.rbegin(); it != m_impl->contextStack.rend(); ++it) {
					if (*it) triggerActions(**it);
				}
				if (m_impl->defaultContext) {
					triggerActions(*m_impl->defaultContext);
				}

				m_unhandledEvents.push_back(event);
			}

			m_allEvents.push_back(event);
		}

		m_rawEvents.clear();
	}

	SubscriptionId InputSystem::AddSubscriber(SharedPtr<IInputSubscriber> subscriber, int32_t priority, StringView name) {
		if (!m_impl || !subscriber) return 0;
		const SubscriptionId id = s_nextSubscriptionId.fetch_add(1, std::memory_order_relaxed);
		RegisteredSubscriber sub;
		sub.id = id;
		sub.priority = priority;
		sub.name = String(name);
		sub.sharedSubscriber = std::move(subscriber);
		m_impl->subscribers.push_back(std::move(sub));
		m_impl->SortSubscribers();
		return id;
	}

	SubscriptionId InputSystem::AddSubscriber(IInputSubscriber* subscriber, int32_t priority, StringView name) {
		if (!m_impl || !subscriber) return 0;
		const SubscriptionId id = s_nextSubscriptionId.fetch_add(1, std::memory_order_relaxed);
		RegisteredSubscriber sub;
		sub.id = id;
		sub.priority = priority;
		sub.name = String(name);
		sub.rawSubscriber = subscriber;
		m_impl->subscribers.push_back(std::move(sub));
		m_impl->SortSubscribers();
		return id;
	}

	SubscriptionId InputSystem::AddListener(EventCallback callback, int32_t priority, StringView name) {
		if (!m_impl || !callback) return 0;
		const SubscriptionId id = s_nextSubscriptionId.fetch_add(1, std::memory_order_relaxed);
		RegisteredSubscriber sub;
		sub.id = id;
		sub.priority = priority;
		sub.name = String(name);
		sub.callback = std::move(callback);
		m_impl->subscribers.push_back(std::move(sub));
		m_impl->SortSubscribers();
		return id;
	}

	ScopedInputSubscription InputSystem::SubscribeScoped(SharedPtr<IInputSubscriber> subscriber, int32_t priority, StringView name) {
		SubscriptionId id = AddSubscriber(std::move(subscriber), priority, name);
		return ScopedInputSubscription(this, id);
	}

	ScopedInputSubscription InputSystem::SubscribeScoped(IInputSubscriber* subscriber, int32_t priority, StringView name) {
		SubscriptionId id = AddSubscriber(subscriber, priority, name);
		return ScopedInputSubscription(this, id);
	}

	ScopedInputSubscription InputSystem::SubscribeScoped(EventCallback callback, int32_t priority, StringView name) {
		SubscriptionId id = AddListener(std::move(callback), priority, name);
		return ScopedInputSubscription(this, id);
	}

	void InputSystem::RemoveSubscriber(SubscriptionId id) {
		if (!m_impl || id == 0) return;
		auto it = std::remove_if(m_impl->subscribers.begin(), m_impl->subscribers.end(),
			[id](const RegisteredSubscriber& sub) noexcept { return sub.id == id; });
		m_impl->subscribers.erase(it, m_impl->subscribers.end());
	}

	void InputSystem::RemoveSubscriber(IInputSubscriber* subscriber) {
		if (!m_impl || !subscriber) return;
		auto it = std::remove_if(m_impl->subscribers.begin(), m_impl->subscribers.end(),
			[subscriber](const RegisteredSubscriber& sub) noexcept {
				return sub.rawSubscriber == subscriber || sub.sharedSubscriber.get() == subscriber;
			});
		m_impl->subscribers.erase(it, m_impl->subscribers.end());
	}

	// Immediate state polling
	bool InputSystem::IsKeyDown(Key key) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<int>(key);
		if (idx < 0 || static_cast<size_t>(idx) >= m_impl->currentKeys.size()) return false;
		return m_impl->currentKeys[static_cast<size_t>(idx)];
	}

	bool InputSystem::IsKeyPressed(Key key) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<int>(key);
		if (idx < 0 || static_cast<size_t>(idx) >= m_impl->currentKeys.size()) return false;
		return m_impl->currentKeys[static_cast<size_t>(idx)] && !m_impl->previousKeys[static_cast<size_t>(idx)];
	}

	bool InputSystem::IsKeyReleased(Key key) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<int>(key);
		if (idx < 0 || static_cast<size_t>(idx) >= m_impl->currentKeys.size()) return false;
		return !m_impl->currentKeys[static_cast<size_t>(idx)] && m_impl->previousKeys[static_cast<size_t>(idx)];
	}

	bool InputSystem::IsMouseButtonDown(MouseButton button) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<size_t>(button);
		if (idx >= m_impl->currentMouseButtons.size()) return false;
		return m_impl->currentMouseButtons[idx];
	}

	bool InputSystem::IsMouseButtonPressed(MouseButton button) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<size_t>(button);
		if (idx >= m_impl->currentMouseButtons.size()) return false;
		return m_impl->currentMouseButtons[idx] && !m_impl->previousMouseButtons[idx];
	}

	bool InputSystem::IsMouseButtonReleased(MouseButton button) const noexcept {
		if (!m_impl) return false;
		const auto idx = static_cast<size_t>(button);
		if (idx >= m_impl->currentMouseButtons.size()) return false;
		return !m_impl->currentMouseButtons[idx] && m_impl->previousMouseButtons[idx];
	}

	double InputSystem::GetMouseX() const noexcept {
		return m_impl ? m_impl->mouseX : 0.0;
	}

	double InputSystem::GetMouseY() const noexcept {
		return m_impl ? m_impl->mouseY : 0.0;
	}

	std::pair<double, double> InputSystem::GetMousePosition() const noexcept {
		return m_impl ? std::make_pair(m_impl->mouseX, m_impl->mouseY) : std::make_pair(0.0, 0.0);
	}

	std::pair<double, double> InputSystem::GetMouseDelta() const noexcept {
		return m_impl ? std::make_pair(m_impl->mouseDeltaX, m_impl->mouseDeltaY) : std::make_pair(0.0, 0.0);
	}

	std::pair<double, double> InputSystem::GetScrollDelta() const noexcept {
		return m_impl ? std::make_pair(m_impl->scrollDeltaX, m_impl->scrollDeltaY) : std::make_pair(0.0, 0.0);
	}

	// Action & Axis contexts
	void InputSystem::PushContext(SharedPtr<InputContext> context) {
		if (m_impl && context) {
			m_impl->contextStack.push_back(std::move(context));
		}
	}

	void InputSystem::PopContext() {
		if (m_impl && !m_impl->contextStack.empty()) {
			m_impl->contextStack.pop_back();
		}
	}

	InputContext& InputSystem::GetDefaultContext() noexcept {
		return *m_impl->defaultContext;
	}

	void InputSystem::AddActionMapping(StringView actionName, Key key, KeyAction triggerAction, KeyModifiers modifiers) {
		if (m_impl && m_impl->defaultContext) {
			m_impl->defaultContext->AddActionMapping(actionName, key, triggerAction, modifiers);
		}
	}

	void InputSystem::AddActionMapping(StringView actionName, MouseButton button, KeyAction triggerAction, KeyModifiers modifiers) {
		if (m_impl && m_impl->defaultContext) {
			m_impl->defaultContext->AddActionMapping(actionName, button, triggerAction, modifiers);
		}
	}

	void InputSystem::AddAxisMapping(StringView axisName, Key key, float scale) {
		if (m_impl && m_impl->defaultContext) {
			m_impl->defaultContext->AddAxisMapping(axisName, key, scale);
		}
	}

	void InputSystem::BindAction(StringView actionName, ActionCallback callback) {
		if (m_impl && m_impl->defaultContext) {
			m_impl->defaultContext->BindAction(actionName, std::move(callback));
		}
	}

	float InputSystem::GetAxisValue(StringView axisName) const {
		if (!m_impl) return 0.0f;

		auto evaluateContext = [this, axisName](const InputContext& ctx) -> std::pair<bool, float> {
			if (!ctx.IsActive()) return { false, 0.0f };
			const auto& axes = ctx.GetAxes();
			auto it = axes.find(String(axisName));
			if (it == axes.end()) return { false, 0.0f };

			float val = 0.0f;
			for (const auto& mapping : it->second.keyMappings) {
				if (IsKeyDown(mapping.key)) {
					val += mapping.scale;
				}
			}
			return { true, val };
			};

		// Check top of stack down to default
		for (auto it = m_impl->contextStack.rbegin(); it != m_impl->contextStack.rend(); ++it) {
			if (*it) {
				auto [found, val] = evaluateContext(**it);
				if (found) return val;
			}
		}

		if (m_impl->defaultContext) {
			auto [found, val] = evaluateContext(*m_impl->defaultContext);
			if (found) return val;
		}

		return 0.0f;
	}

} // namespace elm