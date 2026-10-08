#include "platform/Window.hpp"

#if PLATFORM_WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>

namespace elm::platform {

Window::~Window()
{
    Destroy();
}

auto Window::Create(Size size, StringView title) -> EngineResult<void>
{
    if (m_window)
        return {};

#if PLATFORM_WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    if (!glfwInit())
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to initialize GLFW"));
    m_glfwInitialized = true;

    // The renderer owns the graphics API, GLFW only provides the window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(size.width), static_cast<int>(size.height), String(title).c_str(), nullptr, nullptr);
    if (!m_window) {
        Destroy();
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to create GLFW window"));
    }
    return {};
}

void Window::Destroy()
{
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_glfwInitialized) {
        glfwTerminate();
        m_glfwInitialized = false;
    }
}

bool Window::ShouldClose() const
{
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

render::NativeWindow Window::GetNativeWindow() const
{
    return GetNativeWindow(m_window);
}

render::NativeWindow Window::GetNativeWindow(GLFWwindow* window)
{
    render::NativeWindow native;
    if (!window)
        return native;
#if PLATFORM_WIN32
    native.handle = glfwGetWin32Window(window);
    native.platform = render::NativeWindow::Platform::Win32;
#else
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        native.display = glfwGetWaylandDisplay();
        native.handle = glfwGetWaylandWindow(window);
        native.platform = render::NativeWindow::Platform::Wayland;
    } else {
        native.display = glfwGetX11Display();
        native.handle = reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(window)));
        native.platform = render::NativeWindow::Platform::X11;
    }
#endif
    return native;
}

Size Window::GetSize() const
{
    int width = 0;
    int height = 0;
    if (m_window)
        glfwGetWindowSize(m_window, &width, &height);
    return { static_cast<uint32_t>((std::max)(width, 0)), static_cast<uint32_t>((std::max)(height, 0)) };
}

Size Window::GetFramebufferSize() const
{
    int width = 0;
    int height = 0;
    if (m_window)
        glfwGetFramebufferSize(m_window, &width, &height);
    return { static_cast<uint32_t>((std::max)(width, 0)), static_cast<uint32_t>((std::max)(height, 0)) };
}

} // namespace elm::platform
