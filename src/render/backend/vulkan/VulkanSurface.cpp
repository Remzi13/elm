#include "render/backend/vulkan/VulkanSurface.hpp"

#include "core/Log.hpp"

// Platform headers stay in this file: Xlib defines macros (None, Bool, Status) that clash with engine code
#include <X11/Xlib.h>
#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>

#include <cstdint>

namespace elm::render::vulkan {

const char* SurfaceExtensionName(WindowSystem system) noexcept
{
    switch (system) {
    case WindowSystem::X11:
        return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
    case WindowSystem::Wayland:
        return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    case WindowSystem::Win32:
    default:
        return nullptr;
    }
}

VkSurfaceKHR CreateNativeSurface(VkInstance instance, const NativeWindow& window)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (window.system == WindowSystem::X11) {
        VkXlibSurfaceCreateInfoKHR info {};
        info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        info.dpy = static_cast<Display*>(window.display);
        // GLFW hands the X11 Window id over in a pointer-sized handle
        info.window = static_cast<::Window>(reinterpret_cast<uintptr_t>(window.handle));
        result = vkCreateXlibSurfaceKHR(instance, &info, nullptr, &surface);
    } else if (window.system == WindowSystem::Wayland) {
        VkWaylandSurfaceCreateInfoKHR info {};
        info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.display = static_cast<wl_display*>(window.display);
        info.surface = static_cast<wl_surface*>(window.handle);
        result = vkCreateWaylandSurfaceKHR(instance, &info, nullptr, &surface);
    }
    if (result != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, "VulkanBackend", "Failed to create window surface (VkResult %d)", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return surface;
}

} // namespace elm::render::vulkan
