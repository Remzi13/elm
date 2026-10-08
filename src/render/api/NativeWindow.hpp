#pragma once

#include <cstdint>

namespace elm::render {

    /// OS window a surface presents to: HWND on Windows; X11 Window id or wl_surface plus the display on Linux.
    struct NativeWindow {
        enum class Platform : uint8_t {
            Unknown,
            Win32,
            X11,
            Wayland,
        };

        void* handle { nullptr };
        void* display { nullptr };
        Platform platform { Platform::Unknown };

        [[nodiscard]] bool IsValid() const noexcept { return handle != nullptr; }
    };

} // namespace elm::render
