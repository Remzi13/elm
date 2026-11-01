#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "render/backend/vulkan/VulkanMemory.hpp"
#include "render/backend/vulkan/VulkanPipeline.hpp"
#include "render/rhi/UploadAllocator.hpp"

#include <vulkan/vulkan.h>

#include <optional>
#include <span>

namespace elm::render::vulkan {

    class VulkanDevice;

    /// A range of a GPU buffer the frame wrote from the CPU.
    struct BufferRange {
        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceSize offset { 0 };
        VkDeviceSize size { 0 };
        uint8_t* data { nullptr };
    };

    /// One frame in flight: its command buffer and fence, the host-visible memory its draws read
    /// (upload arenas, constants, staging for copies) and the resources retired while the GPU may still use them.
    ///
    /// Owned by VulkanBackend; render thread. Memory written for a frame stays valid until Begin is called
    /// for it again, after its fence signalled.
    class VulkanFrame {
    public:
        VulkanFrame() = default;
        ~VulkanFrame();

        VulkanFrame(const VulkanFrame&) = delete;
        VulkanFrame& operator=(const VulkanFrame&) = delete;
        VulkanFrame(VulkanFrame&&) = delete;
        VulkanFrame& operator=(VulkanFrame&&) = delete;

        [[nodiscard]] auto Init(VulkanDevice& device) -> EngineResult<void>;
        void Shutdown() noexcept;

        /// Waits for the GPU to finish this frame's previous submission, frees what was retired then and starts recording.
        void Begin();
        [[nodiscard]] VkCommandBuffer GetCommands() const noexcept { return m_commands; }
        [[nodiscard]] VkFence GetFence() const noexcept { return m_fence; }

        /// GPU buffer holding a copy of the used part of the arena. Constants are not copied whole, see WriteConstants.
        [[nodiscard]] BufferRange WriteArena(rhi::UploadArena arena, std::span<const uint8_t> bytes);
        /// Copy of one constants block at the alignment uniform buffers need. nullopt when the frame is out of space.
        [[nodiscard]] std::optional<BufferRange> WriteConstants(std::span<const uint8_t> bytes);
        /// Host memory a copy command reads from; grows as needed.
        [[nodiscard]] BufferRange AllocateStaging(VkDeviceSize size);

        void Retire(VulkanBuffer buffer) { m_retiredBuffers.push_back(std::move(buffer)); }
        void Retire(VulkanImage image) { m_retiredImages.push_back(std::move(image)); }
        void Retire(VulkanPipeline pipeline) { m_retiredPipelines.push_back(std::move(pipeline)); }

    private:
        /// Host-visible buffer filled front to back during the frame.
        struct LinearBuffer {
            VulkanBuffer buffer;
            VkDeviceSize used { 0 };
        };

        void ReleaseRetired() noexcept;

        VulkanDevice* m_device { nullptr };
        VkCommandPool m_pool { VK_NULL_HANDLE };
        VkCommandBuffer m_commands { VK_NULL_HANDLE };
        VkFence m_fence { VK_NULL_HANDLE };

        VulkanBuffer m_vertexArena;
        VulkanBuffer m_indexArena;
        LinearBuffer m_constants;
        LinearBuffer m_staging;

        Vector<VulkanBuffer> m_retiredBuffers;
        Vector<VulkanImage> m_retiredImages;
        Vector<VulkanPipeline> m_retiredPipelines;
    };

} // namespace elm::render::vulkan
