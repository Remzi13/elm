#pragma once

#include "input/InputTypes.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace elm {

	struct ActionKeyMapping {
		Key key{ Key::Unknown };
		KeyAction triggerAction{ KeyAction::Press };
		KeyModifiers requiredModifiers{ KeyModifiers::None };
	};

	struct ActionMouseButtonMapping {
		MouseButton button{ MouseButton::Left };
		KeyAction triggerAction{ KeyAction::Press };
		KeyModifiers requiredModifiers{ KeyModifiers::None };
	};

	struct AxisKeyMapping {
		Key key{ Key::Unknown };
		float scale{ 1.0f };
	};

	using ActionCallback = std::function<void()>;

	struct ActionBinding {
		String name;
		Vector<ActionKeyMapping> keyMappings;
		Vector<ActionMouseButtonMapping> mouseMappings;
		Vector<ActionCallback> callbacks;
	};

	struct AxisBinding {
		String name;
		Vector<AxisKeyMapping> keyMappings;
	};

	class InputContext {
	public:
		explicit InputContext(String name = "DefaultContext") : m_name(std::move(name)) {}

		[[nodiscard]] const String& GetName() const noexcept { return m_name; }
		[[nodiscard]] bool IsActive() const noexcept { return m_active; }
		void SetActive(bool active) noexcept { m_active = active; }

		void AddActionMapping(StringView actionName, Key key, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None) {
			auto& binding = GetOrCreateAction(actionName);
			binding.keyMappings.push_back({ .key = key, .triggerAction = triggerAction, .requiredModifiers = modifiers });
		}

		void AddActionMapping(StringView actionName, MouseButton button, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None) {
			auto& binding = GetOrCreateAction(actionName);
			binding.mouseMappings.push_back({ .button = button, .triggerAction = triggerAction, .requiredModifiers = modifiers });
		}

		void AddAxisMapping(StringView axisName, Key key, float scale) {
			auto& binding = GetOrCreateAxis(axisName);
			binding.keyMappings.push_back({ .key = key, .scale = scale });
		}

		void BindAction(StringView actionName, ActionCallback callback) {
			auto& binding = GetOrCreateAction(actionName);
			binding.callbacks.push_back(std::move(callback));
		}

		[[nodiscard]] const std::unordered_map<String, ActionBinding>& GetActions() const noexcept {
			return m_actions;
		}

		[[nodiscard]] const std::unordered_map<String, AxisBinding>& GetAxes() const noexcept {
			return m_axes;
		}

	private:
		ActionBinding& GetOrCreateAction(StringView name) {
			auto it = m_actions.find(String(name));
			if (it != m_actions.end()) return it->second;
			ActionBinding binding;
			binding.name = String(name);
			auto [inserted, _] = m_actions.emplace(String(name), std::move(binding));
			return inserted->second;
		}

		AxisBinding& GetOrCreateAxis(StringView name) {
			auto it = m_axes.find(String(name));
			if (it != m_axes.end()) return it->second;
			AxisBinding binding;
			binding.name = String(name);
			auto [inserted, _] = m_axes.emplace(String(name), std::move(binding));
			return inserted->second;
		}

	private:
		String m_name;
		bool m_active{ true };
		std::unordered_map<String, ActionBinding> m_actions;
		std::unordered_map<String, AxisBinding> m_axes;
	};

} // namespace elm
