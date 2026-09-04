#pragma once

#include "input/InputTypes.hpp"
#include "input/InputSubscriber.hpp"
#include "input/InputAction.hpp"

#include <memory>
#include <vector>
#include <string_view>
#include <utility>

namespace elm {

	class InputSystem {
	public:
		InputSystem();
		~InputSystem();

		InputSystem(const InputSystem&) = delete;
		InputSystem& operator=(const InputSystem&) = delete;
		InputSystem(InputSystem&&) noexcept;
		InputSystem& operator=(InputSystem&&) noexcept;

		// Window management
		void AttachWindow(void* window);
		[[nodiscard]] void* GetAttachedWindow() const noexcept;

		// Frame lifecycle
		void BeginFrame() noexcept;
		void Update(bool allowInput = true);

		// =========================================================================
		// Prioritized Subscriber Pipeline
		// =========================================================================
		/**
		 * @brief Registers an object subscriber with a given priority.
		 * Higher priority subscribers receive events first. If a subscriber returns true,
		 * propagation stops immediately and lower-priority subscribers will NOT receive it.
		 */
		SubscriptionId AddSubscriber(
			std::shared_ptr<IInputSubscriber> subscriber,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		SubscriptionId AddSubscriber(
			IInputSubscriber* subscriber,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		/**
		 * @brief Registers a functional/lambda listener with priority.
		 */
		SubscriptionId AddListener(
			EventCallback callback,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		/**
		 * @brief Helper to obtain an RAII subscription handle.
		 */
		[[nodiscard]] ScopedInputSubscription SubscribeScoped(
			std::shared_ptr<IInputSubscriber> subscriber,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		[[nodiscard]] ScopedInputSubscription SubscribeScoped(
			IInputSubscriber* subscriber,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		[[nodiscard]] ScopedInputSubscription SubscribeScoped(
			EventCallback callback,
			int32_t priority = static_cast<int32_t>(InputPriority::Default),
			std::string_view name = "");

		void RemoveSubscriber(SubscriptionId id);
		void RemoveSubscriber(IInputSubscriber* subscriber);

		/**
		 * @brief Manually dispatches an event through the prioritized subscriber pipeline.
		 * @return true if the event was consumed by a subscriber.
		 */
		bool DispatchEvent(InputEvent& event);

		// =========================================================================
		// Immediate-Mode State Polling (AAA Fast-path Queries)
		// =========================================================================
		[[nodiscard]] bool IsKeyDown(Key key) const noexcept;
		[[nodiscard]] bool IsKeyPressed(Key key) const noexcept;
		[[nodiscard]] bool IsKeyReleased(Key key) const noexcept;

		[[nodiscard]] bool IsMouseButtonDown(MouseButton button) const noexcept;
		[[nodiscard]] bool IsMouseButtonPressed(MouseButton button) const noexcept;
		[[nodiscard]] bool IsMouseButtonReleased(MouseButton button) const noexcept;

		[[nodiscard]] double GetMouseX() const noexcept;
		[[nodiscard]] double GetMouseY() const noexcept;
		[[nodiscard]] std::pair<double, double> GetMousePosition() const noexcept;
		[[nodiscard]] std::pair<double, double> GetMouseDelta() const noexcept;
		[[nodiscard]] std::pair<double, double> GetScrollDelta() const noexcept;

		// =========================================================================
		// Action & Axis Mapping (Unreal-style Enhanced Input)
		// =========================================================================
		void PushContext(std::shared_ptr<InputContext> context);
		void PopContext();
		[[nodiscard]] InputContext& GetDefaultContext() noexcept;

		void AddActionMapping(std::string_view actionName, Key key, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None);
		void AddActionMapping(std::string_view actionName, MouseButton button, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None);
		void AddAxisMapping(std::string_view axisName, Key key, float scale);
		void BindAction(std::string_view actionName, ActionCallback callback);
		[[nodiscard]] float GetAxisValue(std::string_view axisName) const;

		// =========================================================================
		// Event Accessors
		// =========================================================================
		/// Returns unconsumed events from the current frame (preserves backward compatibility)
		[[nodiscard]] const std::vector<InputEvent>& GetEvents() const noexcept { return m_unhandledEvents; }
		[[nodiscard]] const std::vector<InputEvent>& GetUnhandledEvents() const noexcept { return m_unhandledEvents; }
		[[nodiscard]] const std::vector<InputEvent>& GetAllEvents() const noexcept { return m_allEvents; }

	private:
		std::vector<InputEvent> m_rawEvents;
		std::vector<InputEvent> m_allEvents;
		std::vector<InputEvent> m_unhandledEvents;

		std::unique_ptr<struct InputSystemImpl> m_impl;
	};

} // namespace elm