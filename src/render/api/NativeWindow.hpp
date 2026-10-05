#pragma once

namespace elm::render {

    /// OS window a surface presents to: HWND on Windows; X11 Window id or wl_surface plus the display on Linux.
    struct NativeWindow {
        void* handle { nullptr };
        void* display { nullptr };

        [[nodiscard]] bool IsValid() const noexcept { return handle != nullptr; }
    };

} // namespace elm::render
