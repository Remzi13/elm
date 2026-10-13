#include "script/bindings/Bindings.hpp"

#include "input/Input.hpp"

#include <tuple>

namespace elm::script::bindings {

    namespace {

        InputSystem& Input(lua_State* L) { return RequireSystem(L, Engine(L).input, "elm.input"); }

        bool IsDown(lua_State* L, Key key) { return Input(L).IsKeyDown(key); }
        bool IsPressed(lua_State* L, Key key) { return Input(L).IsKeyPressed(key); }
        bool IsReleased(lua_State* L, Key key) { return Input(L).IsKeyReleased(key); }
        bool IsMouseDown(lua_State* L, MouseButton button) { return Input(L).IsMouseButtonDown(button); }
        bool IsMousePressed(lua_State* L, MouseButton button) { return Input(L).IsMouseButtonPressed(button); }
        bool IsMouseReleased(lua_State* L, MouseButton button) { return Input(L).IsMouseButtonReleased(button); }
        float Axis(lua_State* L, StringView name) { return Input(L).GetAxisValue(name); }

        std::tuple<double, double> MousePosition(lua_State* L)
        {
            const auto [x, y] = Input(L).GetMousePosition();
            return { x, y };
        }
        std::tuple<double, double> MouseDelta(lua_State* L)
        {
            const auto [x, y] = Input(L).GetMouseDelta();
            return { x, y };
        }
        std::tuple<double, double> ScrollDelta(lua_State* L)
        {
            const auto [x, y] = Input(L).GetScrollDelta();
            return { x, y };
        }

        struct NamedKey {
            const char* name;
            Key key;
        };

        constexpr NamedKey NamedKeys[] = {
            { "Space", Key::Space }, { "Apostrophe", Key::Apostrophe }, { "Comma", Key::Comma },
            { "Minus", Key::Minus }, { "Period", Key::Period }, { "Slash", Key::Slash },
            { "Num0", Key::Num0 }, { "Num1", Key::Num1 }, { "Num2", Key::Num2 }, { "Num3", Key::Num3 },
            { "Num4", Key::Num4 }, { "Num5", Key::Num5 }, { "Num6", Key::Num6 }, { "Num7", Key::Num7 },
            { "Num8", Key::Num8 }, { "Num9", Key::Num9 }, { "Semicolon", Key::Semicolon }, { "Equal", Key::Equal },
            { "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D }, { "E", Key::E }, { "F", Key::F },
            { "G", Key::G }, { "H", Key::H }, { "I", Key::I }, { "J", Key::J }, { "K", Key::K }, { "L", Key::L },
            { "M", Key::M }, { "N", Key::N }, { "O", Key::O }, { "P", Key::P }, { "Q", Key::Q }, { "R", Key::R },
            { "S", Key::S }, { "T", Key::T }, { "U", Key::U }, { "V", Key::V }, { "W", Key::W }, { "X", Key::X },
            { "Y", Key::Y }, { "Z", Key::Z }, { "LeftBracket", Key::LeftBracket }, { "Backslash", Key::Backslash },
            { "RightBracket", Key::RightBracket }, { "GraveAccent", Key::GraveAccent },
            { "Escape", Key::Escape }, { "Enter", Key::Enter }, { "Tab", Key::Tab }, { "Backspace", Key::Backspace },
            { "Insert", Key::Insert }, { "Delete", Key::Delete }, { "Right", Key::Right }, { "Left", Key::Left },
            { "Down", Key::Down }, { "Up", Key::Up }, { "PageUp", Key::PageUp }, { "PageDown", Key::PageDown },
            { "Home", Key::Home }, { "End", Key::End }, { "CapsLock", Key::CapsLock }, { "Pause", Key::Pause },
            { "F1", Key::F1 }, { "F2", Key::F2 }, { "F3", Key::F3 }, { "F4", Key::F4 }, { "F5", Key::F5 },
            { "F6", Key::F6 }, { "F7", Key::F7 }, { "F8", Key::F8 }, { "F9", Key::F9 }, { "F10", Key::F10 },
            { "F11", Key::F11 }, { "F12", Key::F12 },
            { "KP0", Key::KP0 }, { "KP1", Key::KP1 }, { "KP2", Key::KP2 }, { "KP3", Key::KP3 }, { "KP4", Key::KP4 },
            { "KP5", Key::KP5 }, { "KP6", Key::KP6 }, { "KP7", Key::KP7 }, { "KP8", Key::KP8 }, { "KP9", Key::KP9 },
            { "KPAdd", Key::KPAdd }, { "KPSubtract", Key::KPSubtract }, { "KPMultiply", Key::KPMultiply },
            { "KPDivide", Key::KPDivide }, { "KPEnter", Key::KPEnter },
            { "LeftShift", Key::LeftShift }, { "LeftControl", Key::LeftControl }, { "LeftAlt", Key::LeftAlt },
            { "RightShift", Key::RightShift }, { "RightControl", Key::RightControl }, { "RightAlt", Key::RightAlt },
        };

    } // namespace

    void RegisterInput(lua::State& state)
    {
        lua::Module(state, "elm.input")
            .Function<&IsDown>("is_down", { "key" })
            .Function<&IsPressed>("is_pressed", { "key" })
            .Function<&IsReleased>("is_released", { "key" })
            .Function<&IsMouseDown>("is_mouse_down", { "button" })
            .Function<&IsMousePressed>("is_mouse_pressed", { "button" })
            .Function<&IsMouseReleased>("is_mouse_released", { "button" })
            .Function<&MousePosition>("mouse_position")
            .Function<&MouseDelta>("mouse_delta")
            .Function<&ScrollDelta>("scroll_delta")
            .Function<&Axis>("axis", { "name" });

        // elm.input.key.W, elm.input.mouse.Left: the values the functions above take
        {
            lua::Module keys(state, "elm.input.key");
            for (const auto& [name, key] : NamedKeys)
                keys.Value(name, key);
        }
        lua::Module(state, "elm.input.mouse")
            .Value("Left", MouseButton::Left)
            .Value("Right", MouseButton::Right)
            .Value("Middle", MouseButton::Middle);
    }

} // namespace elm::script::bindings
