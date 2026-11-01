#include "render/backend/vulkan/VulkanMemory.hpp"

#include "core/Log.hpp"

#include "render/backend/vulkan/VulkanDevice.hpp"

#include <utility>

namespace elm::render::vulkan {

namespace {

    constexpr const char* Source = "VulkanBackend";

    struct LayoutAccess {
        VkPipelineStageFlags2 stage { VK_PIPELINE_STAGE_2_NONE };
        VkAccessFlags2 access { VK_ACCESS_2_NONE };
    };

    /// Work that uses an image in this layout: what a barrier waits for (from) or blocks (to).
    LayoutAccess AccessOf(VkImageLayout layout)
    {
        switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT };
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return { VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        case VK_IMAGE_LAYOUT_UNDEFINED:
            // A swapchain image is acquired before color output (the semaphore waits there); presentation needs no access
            return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE };
        default:
            return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };
        }
    }

    VkImageAspectFlags BarrierAspect(VkFormat format)
    {
        switch (format) {
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    VkImageView CreateView(VkDevice device, VkImage image, const ImageCreateDesc& desc)
    {
        VkImageViewCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = desc.format;
        info.subresourceRange = { desc.aspect, 0, 1, 0, 1 };
        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(device, &info, nullptr, &view);
        return view;
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VulkanBuffer
// ─────────────────────────────────────────────────────────────────────────────

VulkanBuffer VulkanBuffer::Create(VulkanDevice& device, const BufferCreateDesc& desc)
{
    VulkanBuffer buffer;
    buffer.m_device = &device;
    buffer.m_size = desc.size;

    VkBufferCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.size;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (desc.size == 0 || vkCreateBuffer(device.Get(), &info, nullptr, &buffer.m_buffer) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Failed to create a buffer of %llu bytes", static_cast<unsigned long long>(desc.size));
        return {};
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device.Get(), buffer.m_buffer, &requirements);
    const bool hostVisible = desc.memory == MemoryKind::HostVisible;
    const VkMemoryPropertyFlags properties = hostVisible
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    buffer.m_memory = device.AllocateMemory(requirements, properties);
    buffer.m_allocationSize = requirements.size;
    if (buffer.m_memory == VK_NULL_HANDLE || vkBindBufferMemory(device.Get(), buffer.m_buffer, buffer.m_memory, 0) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Out of memory for a buffer of %llu bytes", static_cast<unsigned long long>(desc.size));
        return {};
    }

    void* mapped = nullptr;
    if (hostVisible && vkMapMemory(device.Get(), buffer.m_memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
        return {};
    buffer.m_mapped = static_cast<uint8_t*>(mapped);
    return buffer;
}

VulkanBuffer::~VulkanBuffer()
{
    Release();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : m_device(std::exchange(other.m_device, nullptr))
    , m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE))
    , m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE))
    , m_size(std::exchange(other.m_size, 0))
    , m_allocationSize(std::exchange(other.m_allocationSize, 0))
    , m_mapped(std::exchange(other.m_mapped, nullptr))
{
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
{
    if (this != &other) {
        Release();
        m_device = std::exchange(other.m_device, nullptr);
        m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
        m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
        m_size = std::exchange(other.m_size, 0);
        m_allocationSize = std::exchange(other.m_allocationSize, 0);
        m_mapped = std::exchange(other.m_mapped, nullptr);
    }
    return *this;
}

void VulkanBuffer::Release() noexcept
{
    if (!m_device)
        return;
    if (m_buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device->Get(), m_buffer, nullptr);
    // Freeing mapped memory unmaps it implicitly
    m_device->FreeMemory(m_memory, m_allocationSize);
    m_buffer = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_mapped = nullptr;
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// VulkanImage
// ─────────────────────────────────────────────────────────────────────────────

VulkanImage VulkanImage::Create(VulkanDevice& device, const ImageCreateDesc& desc)
{
    VulkanImage image;
    image.m_device = &device;
    image.m_format = desc.format;

    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = { desc.width, desc.height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device.Get(), &info, nullptr, &image.m_image) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Failed to create a %ux%u image", desc.width, desc.height);
        return {};
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device.Get(), image.m_image, &requirements);
    image.m_memory = device.AllocateMemory(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    image.m_allocationSize = requirements.size;
    if (image.m_memory == VK_NULL_HANDLE || vkBindImageMemory(device.Get(), image.m_image, image.m_memory, 0) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Out of memory for a %ux%u image", desc.width, desc.height);
        return {};
    }

    image.m_view = CreateView(device.Get(), image.m_image, desc);
    if (image.m_view == VK_NULL_HANDLE)
        return {};
    return image;
}

VulkanImage::~VulkanImage()
{
    Release();
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
    : m_device(std::exchange(other.m_device, nullptr))
    , m_image(std::exchange(other.m_image, VK_NULL_HANDLE))
    , m_view(std::exchange(other.m_view, VK_NULL_HANDLE))
    , m_memory(std::exchange(other.m_memory, VK_NULL_HANDLE))
    , m_allocationSize(std::exchange(other.m_allocationSize, 0))
    , m_format(std::exchange(other.m_format, VK_FORMAT_UNDEFINED))
    , m_layout(std::exchange(other.m_layout, VK_IMAGE_LAYOUT_UNDEFINED))
{
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
{
    if (this != &other) {
        Release();
        m_device = std::exchange(other.m_device, nullptr);
        m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
        m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
        m_memory = std::exchange(other.m_memory, VK_NULL_HANDLE);
        m_allocationSize = std::exchange(other.m_allocationSize, 0);
        m_format = std::exchange(other.m_format, VK_FORMAT_UNDEFINED);
        m_layout = std::exchange(other.m_layout, VK_IMAGE_LAYOUT_UNDEFINED);
    }
    return *this;
}

void VulkanImage::Transition(VkCommandBuffer commands, VkImageLayout layout)
{
    if (m_layout == layout)
        return;
    RecordImageBarrier(commands, m_image, m_format, m_layout, layout);
    m_layout = layout;
}

void VulkanImage::Release() noexcept
{
    if (!m_device)
        return;
    if (m_view != VK_NULL_HANDLE)
        vkDestroyImageView(m_device->Get(), m_view, nullptr);
    if (m_image != VK_NULL_HANDLE)
        vkDestroyImage(m_device->Get(), m_image, nullptr);
    m_device->FreeMemory(m_memory, m_allocationSize);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void RecordImageBarrier(VkCommandBuffer commands, VkImage image, VkFormat format, VkImageLayout from, VkImageLayout to)
{
    const LayoutAccess source = AccessOf(from);
    const LayoutAccess destination = AccessOf(to);

    VkImageMemoryBarrier2 barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source.stage;
    barrier.srcAccessMask = source.access;
    barrier.dstStageMask = destination.stage;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { BarrierAspect(format), 0, 1, 0, 1 };

    VkDependencyInfo dependency {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void RecordTransferBarrier(VkCommandBuffer commands, TransferBarrier barrier)
{
    VkMemoryBarrier2 memory {};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    if (barrier == TransferBarrier::BeforeCopy) {
        memory.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memory.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        memory.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        memory.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else {
        memory.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        memory.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        memory.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        memory.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    }

    VkDependencyInfo dependency {};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memory;
    vkCmdPipelineBarrier2(commands, &dependency);
}

} // namespace elm::render::vulkan
