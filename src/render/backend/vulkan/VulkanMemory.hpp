#pragma once

#include "core/Std.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace elm::render::vulkan {

    class VulkanDevice;

    enum class MemoryKind : uint8_t {
        /// GPU memory, filled through a staging copy.
        DeviceLocal,
        /// Host-visible coherent memory, mapped for the buffer's whole life.
        HostVisible,
    };

    struct BufferCreateDesc {
        VkDeviceSize size { 0 };
        VkBufferUsageFlags usage { 0 };
        MemoryKind memory { MemoryKind::DeviceLocal };
    };

    /// Buffer with its own device memory. Move-only; destruction frees it at once, so a buffer the GPU
    /// may still read is handed to VulkanFrame::Retire instead. The device outlives every buffer.
    class VulkanBuffer {
    public:
        VulkanBuffer() = default;
        /// Invalid (false) buffer on failure, the error is logged.
        [[nodiscard]] static VulkanBuffer Create(VulkanDevice& device, const BufferCreateDesc& desc);
        ~VulkanBuffer();

        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return m_buffer != VK_NULL_HANDLE; }
        [[nodiscard]] VkBuffer Get() const noexcept { return m_buffer; }
        [[nodiscard]] VkDeviceSize GetSize() const noexcept { return m_size; }
        /// Host-visible buffers only, nullptr otherwise.
        [[nodiscard]] uint8_t* GetMapped() const noexcept { return m_mapped; }

    private:
        void Release() noexcept;

        VulkanDevice* m_device { nullptr };
        VkBuffer m_buffer { VK_NULL_HANDLE };
        VkDeviceMemory m_memory { VK_NULL_HANDLE };
        VkDeviceSize m_size { 0 };
        VkDeviceSize m_allocationSize { 0 };
        uint8_t* m_mapped { nullptr };
    };

    struct ImageCreateDesc {
        uint32_t width { 0 };
        uint32_t height { 0 };
        VkFormat format { VK_FORMAT_UNDEFINED };
        VkImageUsageFlags usage { 0 };
        /// Aspect of the view: color or depth.
        VkImageAspectFlags aspect { VK_IMAGE_ASPECT_COLOR_BIT };
    };

    /// 2D image in device memory with its default view, and the layout it was last transitioned to.
    /// Move-only, same lifetime rules as VulkanBuffer.
    class VulkanImage {
    public:
        VulkanImage() = default;
        /// Invalid (false) image on failure, the error is logged.
        [[nodiscard]] static VulkanImage Create(VulkanDevice& device, const ImageCreateDesc& desc);
        ~VulkanImage();

        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;
        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return m_image != VK_NULL_HANDLE; }
        [[nodiscard]] VkImage Get() const noexcept { return m_image; }
        [[nodiscard]] VkImageView GetView() const noexcept { return m_view; }
        [[nodiscard]] VkImageLayout GetLayout() const noexcept { return m_layout; }

        /// Records a barrier to layout unless the image is already in it.
        void Transition(VkCommandBuffer commands, VkImageLayout layout);

    private:
        void Release() noexcept;

        VulkanDevice* m_device { nullptr };
        VkImage m_image { VK_NULL_HANDLE };
        VkImageView m_view { VK_NULL_HANDLE };
        VkDeviceMemory m_memory { VK_NULL_HANDLE };
        VkDeviceSize m_allocationSize { 0 };
        VkFormat m_format { VK_FORMAT_UNDEFINED };
        VkImageLayout m_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    };

    /// Layout transition of a whole image; the stages and accesses follow from the two layouts.
    void RecordImageBarrier(VkCommandBuffer commands, VkImage image, VkFormat format, VkImageLayout from, VkImageLayout to);

    /// Orders all earlier GPU work before transfer writes, or transfer writes before all later reads.
    enum class TransferBarrier : uint8_t {
        BeforeCopy,
        AfterCopy,
    };
    void RecordTransferBarrier(VkCommandBuffer commands, TransferBarrier barrier);

} // namespace elm::render::vulkan
