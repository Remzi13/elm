#pragma once

#include "input/InputTypes.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace elm {

struct ActionKeyMapping {
    Key key{Key::Unknown};
    KeyAction triggerAction{KeyAction::Press};
    KeyModifiers requiredModifiers{KeyModifiers::None};
};

struct ActionMouseButtonMapping {
    MouseButton button{MouseButton::Left};
    KeyAction triggerAction{KeyAction::Press};
    KeyModifiers requiredModifiers{KeyModifiers::None};
};

struct AxisKeyMapping {
    Key key{Key::Unknown};
    float scale{1.0f};
};

using ActionCallback = std::function<void()>;

struct ActionBinding {
    std::string name;
    std::vector<ActionKeyMapping> keyMappings;
    std::vector<ActionMouseButtonMapping> mouseMappings;
    std::vector<ActionCallback> callbacks;
};

struct AxisBinding {
    std::string name;
    std::vector<AxisKeyMapping> keyMappings;
};

class InputContext {
public:
    explicit InputContext(std::string name = "DefaultContext") : m_name(std::move(name)) {}

    [[nodiscard]] const std::string& GetName() const noexcept { return m_name; }
    [[nodiscard]] bool IsActive() const noexcept { return m_active; }
    void SetActive(bool active) noexcept { m_active = active; }

    void AddActionMapping(std::string_view actionName, Key key, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None) {
        auto& binding = GetOrCreateAction(actionName);
        binding.keyMappings.push_back({.key = key, .triggerAction = triggerAction, .requiredModifiers = modifiers});
    }

    void AddActionMapping(std::string_view actionName, MouseButton button, KeyAction triggerAction = KeyAction::Press, KeyModifiers modifiers = KeyModifiers::None) {
        auto& binding = GetOrCreateAction(actionName);
        binding.mouseMappings.push_back({.button = button, .triggerAction = triggerAction, .requiredModifiers = modifiers});
    }

    void AddAxisMapping(std::string_view axisName, Key key, float scale) {
        auto& binding = GetOrCreateAxis(axisName);
        binding.keyMappings.push_back({.key = key, .scale = scale});
    }

    void BindAction(std::string_view actionName, ActionCallback callback) {
        auto& binding = GetOrCreateAction(actionName);
        binding.callbacks.push_back(std::move(callback));
    }

    [[nodiscard]] const std::unordered_map<std::string, ActionBinding>& GetActions() const noexcept {
        return m_actions;
    }

    [[nodiscard]] const std::unordered_map<std::string, AxisBinding>& GetAxes() const noexcept {
        return m_axes;
    }

private:
    ActionBinding& GetOrCreateAction(std::string_view name) {
        auto it = m_actions.find(std::string(name));
        if (it != m_actions.end()) return it->second;
        ActionBinding binding;
        binding.name = std::string(name);
        auto [inserted, _] = m_actions.emplace(std::string(name), std::move(binding));
        return inserted->second;
    }

    AxisBinding& GetOrCreateAxis(std::string_view name) {
        auto it = m_axes.find(std::string(name));
        if (it != m_axes.end()) return it->second;
        AxisBinding binding;
        binding.name = std::string(name);
        auto [inserted, _] = m_axes.emplace(std::string(name), std::move(binding));
        return inserted->second;
    }

private:
    std::string m_name;
    bool m_active{true};
    std::unordered_map<std::string, ActionBinding> m_actions;
    std::unordered_map<std::string, AxisBinding> m_axes;
};

} // namespace elm
