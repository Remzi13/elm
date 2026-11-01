#pragma once

#include "render/api/NativeWindow.hpp"

#include <vulkan/vulkan.h>

namespace elm::render::vulkan {

    /// Instance extension that creates surfaces for windows of this system; nullptr when unsupported.
    [[nodiscard]] const char* SurfaceExtensionName(WindowSystem system) noexcept;

    /// Surface of an X11 or Wayland window. VK_NULL_HANDLE on failure; the caller destroys it.
    [[nodiscard]] VkSurfaceKHR CreateNativeSurface(VkInstance instance, const NativeWindow& window);

} // namespace elm::render::vulkan
