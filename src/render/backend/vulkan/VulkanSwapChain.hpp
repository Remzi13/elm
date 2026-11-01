#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "render/api/NativeWindow.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace elm::render::vulkan {

    class VulkanDevice;

    /// Frames the CPU may record ahead of the GPU.
    inline constexpr uint32_t FramesInFlight = 2;

    /// Presentation surface of one window: VkSurfaceKHR, its swapchain and their semaphores.
    /// An image is acquired when a pass first renders to the surface, waited for and signalled by the
    /// frame submission, then presented. Recreated on resize or when it goes out of date (waits for the GPU).
    ///
    /// Owned by VulkanBackend; render thread. Not movable: the backend keeps it by pointer.
    class VulkanSwapChain {
    public:
        VulkanSwapChain() = default;
        ~VulkanSwapChain();

        VulkanSwapChain(const VulkanSwapChain&) = delete;
        VulkanSwapChain& operator=(const VulkanSwapChain&) = delete;
        VulkanSwapChain(VulkanSwapChain&&) = delete;
        VulkanSwapChain& operator=(VulkanSwapChain&&) = delete;

        /// format VK_FORMAT_UNDEFINED picks the best one (sRGB first); otherwise it is required.
        [[nodiscard]] auto Init(VulkanDevice& device, const NativeWindow& window, VkExtent2D size, VkFormat format) -> EngineResult<void>;
        void Shutdown() noexcept;

        /// Recreates the swapchain when the size changed. A zero size is ignored (minimized window).
        void Resize(VkExtent2D size);

        /// Acquires the next image for this frame (frameSlot < FramesInFlight). False when there is
        /// nothing to render to (minimized, lost); already acquired is true.
        [[nodiscard]] bool Acquire(uint32_t frameSlot);
        /// An image is acquired and the frame that renders to it is not submitted yet.
        [[nodiscard]] bool NeedsSubmit() const noexcept { return m_acquired && !m_submitted; }
        /// Waited by the submission that renders to the acquired image.
        [[nodiscard]] VkSemaphore GetImageAvailable() const noexcept { return m_imageAvailable[m_acquireSlot]; }
        /// Signalled by that submission, waited by Present.
        [[nodiscard]] VkSemaphore GetRenderFinished() const noexcept { return m_renderFinished[m_imageIndex]; }

        [[nodiscard]] VkImage GetImage() const noexcept { return m_images[m_imageIndex]; }
        [[nodiscard]] VkImageView GetView() const noexcept { return m_views[m_imageIndex]; }
        [[nodiscard]] VkImageLayout GetLayout() const noexcept { return m_layout; }
        void SetLayout(VkImageLayout layout) noexcept { m_layout = layout; }
        [[nodiscard]] VkExtent2D GetExtent() const noexcept { return m_extent; }
        [[nodiscard]] VkFormat GetFormat() const noexcept { return m_format; }

        /// Marks the acquired image as submitted: the next Present shows it.
        void MarkSubmitted() noexcept;
        /// Presents a submitted image; does nothing otherwise.
        void Present();

    private:
        [[nodiscard]] bool Create(VkExtent2D size);
        void DestroySwapchain() noexcept;
        [[nodiscard]] VkFormat ChooseFormat(VkFormat required) const;

        VulkanDevice* m_device { nullptr };
        VkSurfaceKHR m_surface { VK_NULL_HANDLE };
        VkSwapchainKHR m_swapchain { VK_NULL_HANDLE };
        VkFormat m_format { VK_FORMAT_UNDEFINED };
        VkExtent2D m_extent { 0, 0 };
        VkExtent2D m_requestedExtent { 0, 0 };
        Vector<VkImage> m_images;
        Vector<VkImageView> m_views;
        /// Indexed by frame slot: an acquire may happen before the image's previous presentation ends.
        std::array<VkSemaphore, FramesInFlight> m_imageAvailable {};
        /// Indexed by image: reused only after that image is acquired again.
        Vector<VkSemaphore> m_renderFinished;

        uint32_t m_imageIndex { 0 };
        uint32_t m_acquireSlot { 0 };
        VkImageLayout m_layout { VK_IMAGE_LAYOUT_UNDEFINED };
        bool m_acquired { false };
        bool m_submitted { false };
        bool m_outOfDate { false };
    };

} // namespace elm::render::vulkan
