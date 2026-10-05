#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "math/Primitivs.hpp"

#include "render/api/NativeWindow.hpp"

struct GLFWwindow;

namespace elm::platform {

    /// Main application window (GLFW). Main thread only.
    class Window {
    public:
        Window() = default;
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] auto Create(Size size, StringView title) -> EngineResult<void>;
        void Destroy();

        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] GLFWwindow* GetHandle() const noexcept { return m_window; }
        /// OS handles for the renderer's presentation surface.
        [[nodiscard]] render::NativeWindow GetNativeWindow() const;
        /// Size in screen coordinates.
        [[nodiscard]] Size GetSize() const;
        /// Size in pixels; empty while minimized.
        [[nodiscard]] Size GetFramebufferSize() const;

        /// OS handles of any GLFW window (e.g. a secondary UI window).
        [[nodiscard]] static render::NativeWindow GetNativeWindow(GLFWwindow* window);

    private:
        GLFWwindow* m_window { nullptr };
        bool m_glfwInitialized { false };
    };

} // namespace elm::platform
