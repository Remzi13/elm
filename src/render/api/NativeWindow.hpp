#pragma once

#include <cstdint>

namespace elm::render {

    /// Window system the native handles belong to.
    enum class WindowSystem : uint8_t {
        Win32,
        X11,
        Wayland,
    };

    /// OS window a surface presents to: HWND on Windows; X11 Window id or wl_surface plus the display on Linux.
    struct NativeWindow {
        void* handle { nullptr };
        void* display { nullptr };
        WindowSystem system { WindowSystem::Win32 };

        [[nodiscard]] bool IsValid() const noexcept { return handle != nullptr; }
    };

} // namespace elm::render
