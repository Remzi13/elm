#include "render/backend/vulkan/VulkanFrame.hpp"

#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "render/backend/vulkan/VulkanDevice.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace elm::render::vulkan {

namespace {

    constexpr const char* Source = "VulkanBackend";
    constexpr uint64_t NoTimeout = std::numeric_limits<uint64_t>::max();
    /// Each constants block is padded to the uniform offset alignment (up to 256 bytes), so the copy needs more room than the arena.
    constexpr VkDeviceSize ConstantsCapacity = 4 * rhi::UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Constants);
    constexpr VkDeviceSize InitialStagingCapacity = 4 * 1024 * 1024;

    VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    VulkanBuffer CreateHostBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        return VulkanBuffer::Create(device, { size, usage, MemoryKind::HostVisible });
    }

} // namespace

VulkanFrame::~VulkanFrame()
{
    Shutdown();
}

auto VulkanFrame::Init(VulkanDevice& device) -> EngineResult<void>
{
    m_device = &device;

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = device.GetQueueFamily();
    vkCreateCommandPool(device.Get(), &poolInfo, nullptr, &m_pool);

    VkCommandBufferAllocateInfo bufferInfo {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    bufferInfo.commandPool = m_pool;
    bufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferInfo.commandBufferCount = 1;
    if (m_pool != VK_NULL_HANDLE)
        vkAllocateCommandBuffers(device.Get(), &bufferInfo, &m_commands);

    // Signalled: the first Begin does not wait
    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device.Get(), &fenceInfo, nullptr, &m_fence);

    using rhi::UploadAllocator;
    m_vertexArena = CreateHostBuffer(device, UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_indexArena = CreateHostBuffer(device, UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Index), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m_constants.buffer = CreateHostBuffer(device, ConstantsCapacity, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_staging.buffer = CreateHostBuffer(device, InitialStagingCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    if (m_commands == VK_NULL_HANDLE || m_fence == VK_NULL_HANDLE || !m_vertexArena || !m_indexArena || !m_constants.buffer || !m_staging.buffer)
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create frame resources", log::Category::Render, Source);
    return {};
}

void VulkanFrame::Shutdown() noexcept
{
    if (!m_device)
        return;
    if (m_fence != VK_NULL_HANDLE)
        vkWaitForFences(m_device->Get(), 1, &m_fence, VK_TRUE, NoTimeout);
    ReleaseRetired();
    m_vertexArena = {};
    m_indexArena = {};
    m_constants = {};
    m_staging = {};
    vkDestroyFence(m_device->Get(), m_fence, nullptr);
    vkDestroyCommandPool(m_device->Get(), m_pool, nullptr);
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_commands = VK_NULL_HANDLE;
    m_device = nullptr;
}

void VulkanFrame::Begin()
{
    vkWaitForFences(m_device->Get(), 1, &m_fence, VK_TRUE, NoTimeout);
    vkResetFences(m_device->Get(), 1, &m_fence);
    ReleaseRetired();
    m_constants.used = 0;
    m_staging.used = 0;

    vkResetCommandPool(m_device->Get(), m_pool, 0);
    VkCommandBufferBeginInfo info {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commands, &info);
}

BufferRange VulkanFrame::WriteArena(rhi::UploadArena arena, std::span<const uint8_t> bytes)
{
    const VulkanBuffer& target = arena == rhi::UploadArena::Index ? m_indexArena : m_vertexArena;
    const size_t size = (std::min)(bytes.size(), static_cast<size_t>(target.GetSize()));
    if (size > 0)
        std::memcpy(target.GetMapped(), bytes.data(), size);
    return { target.Get(), 0, size, target.GetMapped() };
}

std::optional<BufferRange> VulkanFrame::WriteConstants(std::span<const uint8_t> bytes)
{
    const VkDeviceSize offset = AlignUp(m_constants.used, m_device->GetLimits().minUniformBufferOffsetAlignment);
    if (offset + bytes.size() > m_constants.buffer.GetSize()) {
        ERROR_MESSAGE(log::Category::Render, Source, "Frame constants buffer is full");
        return std::nullopt;
    }
    std::memcpy(m_constants.buffer.GetMapped() + offset, bytes.data(), bytes.size());
    m_constants.used = offset + bytes.size();
    return BufferRange { m_constants.buffer.Get(), offset, bytes.size(), m_constants.buffer.GetMapped() + offset };
}

BufferRange VulkanFrame::AllocateStaging(VkDeviceSize size)
{
    // Texel copies need 4-byte aligned source offsets
    VkDeviceSize offset = AlignUp(m_staging.used, 16);
    if (offset + size > m_staging.buffer.GetSize()) {
        // The old buffer may hold copies recorded this frame: it is freed with the frame
        const VkDeviceSize capacity = std::bit_ceil((std::max)(size, 2 * m_staging.buffer.GetSize()));
        Retire(std::exchange(m_staging.buffer, CreateHostBuffer(*m_device, capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)));
        offset = 0;
        if (!m_staging.buffer)
            return {};
    }
    m_staging.used = offset + size;
    return { m_staging.buffer.Get(), offset, size, m_staging.buffer.GetMapped() + offset };
}

void VulkanFrame::ReleaseRetired() noexcept
{
    m_retiredPipelines.clear();
    m_retiredImages.clear();
    m_retiredBuffers.clear();
}

} // namespace elm::render::vulkan
