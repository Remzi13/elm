#pragma once

#include "input/InputTypes.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace elm {

enum class InputPriority : int32_t {
    DebugOverlay = 10000,
    UI           = 5000,
    Modal        = 2000,
    Gameplay     = 1000,
    Default      = 0,
    Background   = -1000
};

class IInputSubscriber {
public:
    virtual ~IInputSubscriber() = default;

    /**
     * @brief Main event handler entry point. By default dispatches to specific On* handlers.
     * @return true if the event was handled and should NOT propagate further down the pipeline.
     */
    virtual bool OnInputEvent(InputEvent& event) {
        switch (event.type) {
        case InputEventType::Key:
            return OnKeyEvent(event.AsKeyEvent());
        case InputEventType::MouseButton:
            return OnMouseButtonEvent(event.AsMouseButtonEvent());
        case InputEventType::CursorMove:
            return OnMouseMoveEvent(event.AsMouseMoveEvent());
        case InputEventType::Scroll:
            return OnMouseScrollEvent(event.AsMouseScrollEvent());
        case InputEventType::Character:
            return OnCharEvent(event.AsCharEvent());
        case InputEventType::CursorEnter:
            return OnCursorEnterEvent(event.AsCursorEnterEvent());
        default:
            return false;
        }
    }

    virtual bool OnKeyEvent(const KeyEvent& /*event*/) { return false; }
    virtual bool OnMouseButtonEvent(const MouseButtonEvent& /*event*/) { return false; }
    virtual bool OnMouseMoveEvent(const MouseMoveEvent& /*event*/) { return false; }
    virtual bool OnMouseScrollEvent(const MouseScrollEvent& /*event*/) { return false; }
    virtual bool OnCharEvent(const CharEvent& /*event*/) { return false; }
    virtual bool OnCursorEnterEvent(const CursorEnterEvent& /*event*/) { return false; }
};

using EventCallback = std::function<bool(InputEvent&)>;
using SubscriptionId = uint64_t;

class InputSystem;

/**
 * @brief RAII subscription handle that automatically unregisters from InputSystem when destroyed.
 */
class ScopedInputSubscription {
public:
    ScopedInputSubscription() noexcept = default;
    ScopedInputSubscription(InputSystem* system, SubscriptionId id) noexcept
        : m_system(system), m_id(id) {}

    ~ScopedInputSubscription();

    ScopedInputSubscription(const ScopedInputSubscription&) = delete;
    ScopedInputSubscription& operator=(const ScopedInputSubscription&) = delete;

    ScopedInputSubscription(ScopedInputSubscription&& other) noexcept
        : m_system(other.m_system), m_id(other.m_id) {
        other.m_system = nullptr;
        other.m_id = 0;
    }

    ScopedInputSubscription& operator=(ScopedInputSubscription&& other) noexcept {
        if (this != &other) {
            Reset();
            m_system = other.m_system;
            m_id = other.m_id;
            other.m_system = nullptr;
            other.m_id = 0;
        }
        return *this;
    }

    [[nodiscard]] bool IsValid() const noexcept { return m_system != nullptr && m_id != 0; }
    [[nodiscard]] SubscriptionId GetId() const noexcept { return m_id; }
    void Reset();

private:
    InputSystem* m_system{nullptr};
    SubscriptionId m_id{0};
};

} // namespace elm
