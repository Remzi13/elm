#pragma once

#include <cstdint>
#include <type_traits>

namespace elm {

enum class Key : int16_t {
    Unknown       = -1,

    // Printable keys
    Space         = 32,
    Apostrophe    = 39,  /* ' */
    Comma         = 44,  /* , */
    Minus         = 45,  /* - */
    Period        = 46,  /* . */
    Slash         = 47,  /* / */
    Num0          = 48,
    Num1          = 49,
    Num2          = 50,
    Num3          = 51,
    Num4          = 52,
    Num5          = 53,
    Num6          = 54,
    Num7          = 55,
    Num8          = 56,
    Num9          = 57,
    Semicolon     = 59,  /* ; */
    Equal         = 61,  /* = */
    A             = 65,
    B             = 66,
    C             = 67,
    D             = 68,
    E             = 69,
    F             = 70,
    G             = 71,
    H             = 72,
    I             = 73,
    J             = 74,
    K             = 75,
    L             = 76,
    M             = 77,
    N             = 78,
    O             = 79,
    P             = 80,
    Q             = 81,
    R             = 82,
    S             = 83,
    T             = 84,
    U             = 85,
    V             = 86,
    W             = 87,
    X             = 88,
    Y             = 89,
    Z             = 90,
    LeftBracket   = 91,  /* [ */
    Backslash     = 92,  /* \ */
    RightBracket  = 93,  /* ] */
    GraveAccent   = 96,  /* ` */
    World1        = 161, /* non-US #1 */
    World2        = 162, /* non-US #2 */

    // Function keys
    Escape        = 256,
    Enter         = 257,
    Tab           = 258,
    Backspace     = 259,
    Insert        = 260,
    Delete        = 261,
    Right         = 262,
    Left          = 263,
    Down          = 264,
    Up            = 265,
    PageUp        = 266,
    PageDown      = 267,
    Home          = 268,
    End           = 269,
    CapsLock      = 280,
    ScrollLock    = 281,
    NumLock       = 282,
    PrintScreen   = 283,
    Pause         = 284,
    F1            = 290,
    F2            = 291,
    F3            = 292,
    F4            = 293,
    F5            = 294,
    F6            = 295,
    F7            = 296,
    F8            = 297,
    F9            = 298,
    F10           = 299,
    F11           = 300,
    F12           = 301,
    F13           = 302,
    F14           = 303,
    F15           = 304,
    F16           = 305,
    F17           = 306,
    F18           = 307,
    F19           = 308,
    F20           = 309,
    F21           = 310,
    F22           = 311,
    F23           = 312,
    F24           = 313,
    F25           = 314,

    // Keypad
    KP0           = 320,
    KP1           = 321,
    KP2           = 322,
    KP3           = 323,
    KP4           = 324,
    KP5           = 325,
    KP6           = 326,
    KP7           = 327,
    KP8           = 328,
    KP9           = 329,
    KPDecimal     = 330,
    KPDivide      = 331,
    KPMultiply    = 332,
    KPSubtract    = 333,
    KPAdd         = 334,
    KPEnter       = 335,
    KPEqual       = 336,

    // Modifiers & Controls
    LeftShift     = 340,
    LeftControl   = 341,
    LeftAlt       = 342,
    LeftSuper     = 343,
    RightShift    = 344,
    RightControl  = 345,
    RightAlt      = 346,
    RightSuper    = 347,
    Menu          = 348
};

enum class MouseButton : uint8_t {
    Left    = 0,
    Right   = 1,
    Middle  = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7
};

enum class KeyAction : uint8_t {
    Release = 0,
    Press   = 1,
    Repeat  = 2
};

enum class KeyModifiers : uint8_t {
    None     = 0,
    Shift    = 1 << 0,
    Control  = 1 << 1,
    Alt      = 1 << 2,
    Super    = 1 << 3,
    CapsLock = 1 << 4,
    NumLock  = 1 << 5
};

constexpr KeyModifiers operator|(KeyModifiers a, KeyModifiers b) noexcept {
    return static_cast<KeyModifiers>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr KeyModifiers operator&(KeyModifiers a, KeyModifiers b) noexcept {
    return static_cast<KeyModifiers>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr KeyModifiers operator^(KeyModifiers a, KeyModifiers b) noexcept {
    return static_cast<KeyModifiers>(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
}

constexpr KeyModifiers operator~(KeyModifiers a) noexcept {
    return static_cast<KeyModifiers>(~static_cast<uint8_t>(a));
}

constexpr KeyModifiers& operator|=(KeyModifiers& a, KeyModifiers b) noexcept {
    a = a | b;
    return a;
}

constexpr KeyModifiers& operator&=(KeyModifiers& a, KeyModifiers b) noexcept {
    a = a & b;
    return a;
}

constexpr bool HasModifier(KeyModifiers mods, KeyModifiers flag) noexcept {
    return (mods & flag) == flag;
}

enum class InputEventType : uint8_t {
    Key,
    Character,
    MouseButton,
    CursorMove,
    CursorEnter,
    Scroll
};

// Aliases for backwards compatibility
using InputAction = KeyAction;
using InputKey = Key;
using InputMouseButton = MouseButton;

struct KeyEvent {
    Key key{Key::Unknown};
    int32_t scancode{0};
    KeyAction action{KeyAction::Release};
    KeyModifiers modifiers{KeyModifiers::None};

    [[nodiscard]] constexpr bool IsPress() const noexcept { return action == KeyAction::Press; }
    [[nodiscard]] constexpr bool IsRelease() const noexcept { return action == KeyAction::Release; }
    [[nodiscard]] constexpr bool IsRepeat() const noexcept { return action == KeyAction::Repeat; }
    [[nodiscard]] constexpr bool HasShift() const noexcept { return HasModifier(modifiers, KeyModifiers::Shift); }
    [[nodiscard]] constexpr bool HasControl() const noexcept { return HasModifier(modifiers, KeyModifiers::Control); }
    [[nodiscard]] constexpr bool HasAlt() const noexcept { return HasModifier(modifiers, KeyModifiers::Alt); }
    [[nodiscard]] constexpr bool HasSuper() const noexcept { return HasModifier(modifiers, KeyModifiers::Super); }
};

struct MouseButtonEvent {
    MouseButton button{MouseButton::Left};
    KeyAction action{KeyAction::Release};
    KeyModifiers modifiers{KeyModifiers::None};
    double x{0.0};
    double y{0.0};

    [[nodiscard]] constexpr bool IsPress() const noexcept { return action == KeyAction::Press; }
    [[nodiscard]] constexpr bool IsRelease() const noexcept { return action == KeyAction::Release; }
    [[nodiscard]] constexpr bool IsLeft() const noexcept { return button == MouseButton::Left; }
    [[nodiscard]] constexpr bool IsRight() const noexcept { return button == MouseButton::Right; }
    [[nodiscard]] constexpr bool IsMiddle() const noexcept { return button == MouseButton::Middle; }
};

struct MouseMoveEvent {
    double x{0.0};
    double y{0.0};
    double deltaX{0.0};
    double deltaY{0.0};
};

struct MouseScrollEvent {
    double xOffset{0.0};
    double yOffset{0.0};
};

struct CharEvent {
    uint32_t codepoint{0};
};

struct CursorEnterEvent {
    bool entered{false};
};

struct InputEvent {
    InputEventType type{InputEventType::Key};
    Key key{Key::Unknown};
    int scancode{0};
    InputAction action{InputAction::Release};
    int modifiers{0};
    int button{0};
    unsigned int codepoint{0};
    double x{0.0};
    double y{0.0};
    double deltaX{0.0};
    double deltaY{0.0};
    bool consumed{false};

    [[nodiscard]] constexpr bool IsConsumed() const noexcept { return consumed; }
    constexpr void Consume() noexcept { consumed = true; }
    constexpr void SetConsumed(bool isConsumed) noexcept { consumed = isConsumed; }

    [[nodiscard]] KeyEvent AsKeyEvent() const noexcept {
        return KeyEvent{
            .key = key,
            .scancode = scancode,
            .action = action,
            .modifiers = static_cast<KeyModifiers>(modifiers)
        };
    }

    [[nodiscard]] MouseButtonEvent AsMouseButtonEvent() const noexcept {
        return MouseButtonEvent{
            .button = static_cast<MouseButton>(button),
            .action = action,
            .modifiers = static_cast<KeyModifiers>(modifiers),
            .x = x,
            .y = y
        };
    }

    [[nodiscard]] MouseMoveEvent AsMouseMoveEvent() const noexcept {
        return MouseMoveEvent{
            .x = x,
            .y = y,
            .deltaX = deltaX,
            .deltaY = deltaY
        };
    }

    [[nodiscard]] MouseScrollEvent AsMouseScrollEvent() const noexcept {
        return MouseScrollEvent{
            .xOffset = x,
            .yOffset = y
        };
    }

    [[nodiscard]] CharEvent AsCharEvent() const noexcept {
        return CharEvent{ .codepoint = codepoint };
    }

    [[nodiscard]] CursorEnterEvent AsCursorEnterEvent() const noexcept {
        return CursorEnterEvent{ .entered = action == InputAction::Press };
    }
};

} // namespace elm
