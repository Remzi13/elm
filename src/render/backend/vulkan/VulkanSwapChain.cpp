#include "render/backend/vulkan/VulkanSwapChain.hpp"

#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "render/backend/vulkan/VulkanDevice.hpp"
#include "render/backend/vulkan/VulkanSurface.hpp"

#include <algorithm>
#include <limits>

namespace elm::render::vulkan {

namespace {

    constexpr const char* Source = "VulkanBackend";
    constexpr uint64_t NoTimeout = std::numeric_limits<uint64_t>::max();
    /// sRGB first: the overlay shader encodes for an sRGB target as the Diligent backend did.
    constexpr std::array PreferredFormats { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };

    VkSemaphore CreateSemaphore(VkDevice device)
    {
        VkSemaphoreCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        vkCreateSemaphore(device, &info, nullptr, &semaphore);
        return semaphore;
    }

    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D requested)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return capabilities.currentExtent;
        return { std::clamp(requested.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(requested.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
    }

    VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        for (const auto mode : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR }) {
            if (capabilities.supportedCompositeAlpha & mode)
                return mode;
        }
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkImageView CreateView(VkDevice device, VkImage image, VkFormat format)
    {
        VkImageViewCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(device, &info, nullptr, &view);
        return view;
    }

} // namespace

VulkanSwapChain::~VulkanSwapChain()
{
    Shutdown();
}

auto VulkanSwapChain::Init(VulkanDevice& device, const NativeWindow& window, VkExtent2D size, VkFormat format) -> EngineResult<void>
{
    m_device = &device;
    m_surface = CreateNativeSurface(device.GetInstance(), window);
    VkBool32 presentable = VK_FALSE;
    if (m_surface != VK_NULL_HANDLE)
        vkGetPhysicalDeviceSurfaceSupportKHR(device.GetPhysicalDevice(), device.GetQueueFamily(), m_surface, &presentable);
    if (!presentable)
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "The GPU cannot present to the window", log::Category::Render, Source);

    m_format = ChooseFormat(format);
    if (m_format == VK_FORMAT_UNDEFINED)
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "The window does not support the surface format", log::Category::Render, Source);

    for (auto& semaphore : m_imageAvailable)
        semaphore = CreateSemaphore(device.Get());
    m_requestedExtent = size;
    if (!Create(size))
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create the swapchain", log::Category::Render, Source);
    return {};
}

void VulkanSwapChain::Shutdown() noexcept
{
    if (!m_device)
        return;
    m_device->WaitIdle();
    DestroySwapchain();
    for (auto& semaphore : m_imageAvailable) {
        vkDestroySemaphore(m_device->Get(), semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_device->GetInstance(), m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
    m_device = nullptr;
}

VkFormat VulkanSwapChain::ChooseFormat(VkFormat required) const
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->GetPhysicalDevice(), m_surface, &count, nullptr);
    Vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->GetPhysicalDevice(), m_surface, &count, formats.data());

    const auto supported = [&](VkFormat format) {
        return std::ranges::any_of(formats, [format](const VkSurfaceFormatKHR& surface) {
            return surface.format == format && surface.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    };
    if (required != VK_FORMAT_UNDEFINED)
        return supported(required) ? required : VK_FORMAT_UNDEFINED;
    for (const VkFormat format : PreferredFormats) {
        if (supported(format))
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

bool VulkanSwapChain::Create(VkExtent2D size)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->GetPhysicalDevice(), m_surface, &capabilities);
    const VkExtent2D extent = ChooseExtent(capabilities, size);
    m_device->WaitIdle();
    DestroySwapchain();
    m_outOfDate = false;
    m_extent = extent;
    if (extent.width == 0 || extent.height == 0)
        return true;

    VkSwapchainCreateInfoKHR info {};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = m_surface;
    info.minImageCount = capabilities.maxImageCount == 0 ? capabilities.minImageCount + 1
                                                         : (std::min)(capabilities.minImageCount + 1, capabilities.maxImageCount);
    info.imageFormat = m_format;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = ChooseCompositeAlpha(capabilities);
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_device->Get(), &info, nullptr, &m_swapchain) != VK_SUCCESS)
        return false;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_device->Get(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(m_device->Get(), m_swapchain, &count, m_images.data());
    for (const VkImage image : m_images) {
        m_views.push_back(CreateView(m_device->Get(), image, m_format));
        m_renderFinished.push_back(CreateSemaphore(m_device->Get()));
    }
    return true;
}

void VulkanSwapChain::DestroySwapchain() noexcept
{
    for (const VkImageView view : m_views)
        vkDestroyImageView(m_device->Get(), view, nullptr);
    for (const VkSemaphore semaphore : m_renderFinished)
        vkDestroySemaphore(m_device->Get(), semaphore, nullptr);
    if (m_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(m_device->Get(), m_swapchain, nullptr);
    m_views.clear();
    m_renderFinished.clear();
    m_images.clear();
    m_swapchain = VK_NULL_HANDLE;
    m_acquired = false;
    m_submitted = false;
}

void VulkanSwapChain::Resize(VkExtent2D size)
{
    if (size.width == 0 || size.height == 0)
        return;
    const bool changed = size.width != m_requestedExtent.width || size.height != m_requestedExtent.height;
    m_requestedExtent = size;
    if (changed && (size.width != m_extent.width || size.height != m_extent.height))
        static_cast<void>(Create(size));
}

bool VulkanSwapChain::Acquire(uint32_t frameSlot)
{
    if (NeedsSubmit())
        return true;
    // An image submitted but never presented would keep its semaphore signalled
    Present();
    if ((m_outOfDate || m_swapchain == VK_NULL_HANDLE) && !Create(m_requestedExtent))
        return false;
    if (m_swapchain == VK_NULL_HANDLE)
        return false;

    VkResult result = vkAcquireNextImageKHR(m_device->Get(), m_swapchain, NoTimeout, m_imageAvailable[frameSlot], VK_NULL_HANDLE, &m_imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!Create(m_requestedExtent) || m_swapchain == VK_NULL_HANDLE)
            return false;
        result = vkAcquireNextImageKHR(m_device->Get(), m_swapchain, NoTimeout, m_imageAvailable[frameSlot], VK_NULL_HANDLE, &m_imageIndex);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        WARNING_MESSAGE(log::Category::Render, Source, "Failed to acquire a swapchain image (VkResult %d)", static_cast<int>(result));
        return false;
    }
    m_outOfDate = result == VK_SUBOPTIMAL_KHR;
    m_acquireSlot = frameSlot;
    m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_acquired = true;
    return true;
}

void VulkanSwapChain::MarkSubmitted() noexcept
{
    m_submitted = m_acquired;
}

void VulkanSwapChain::Present()
{
    if (!m_submitted)
        return;
    const VkSemaphore renderFinished = GetRenderFinished();
    VkPresentInfoKHR info {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &renderFinished;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &m_imageIndex;
    const VkResult result = vkQueuePresentKHR(m_device->GetQueue(), &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_outOfDate = true;
    m_submitted = false;
    m_acquired = false;
}

} // namespace elm::render::vulkan
