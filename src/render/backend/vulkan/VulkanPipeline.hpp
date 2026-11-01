#pragma once

#include "core/Std.hpp"

#include "render/api/PipelineDesc.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace elm::render::vulkan {

    class VulkanDevice;
    struct SpirvShader;

    struct VulkanPipelineCreateInfo {
        const PipelineDesc& desc;
        const SpirvShader& vertexShader;
        const SpirvShader& pixelShader;
        /// Immutable sampler of every PipelineDesc::samplers entry.
        VkSampler sampler { VK_NULL_HANDLE };
    };

    /// Graphics pipeline for dynamic rendering, with its layout. Resources are pushed per draw
    /// (VK_KHR_push_descriptor): PipelineDesc::bindings[i] is binding i of set 0, sampler j is binding
    /// bindings.size() + j. Viewport and scissor are dynamic.
    ///
    /// Move-only; destruction is immediate, a pipeline the GPU may still use goes to VulkanFrame::Retire.
    class VulkanPipeline {
    public:
        /// One PipelineDesc::bindings entry; its index is the slot of SetConstants/BindTexture.
        struct Slot {
            BindingType type { BindingType::Constants };
            uint32_t size { 0 };
        };

        VulkanPipeline() = default;
        /// Invalid (false) pipeline on failure, the error is logged.
        [[nodiscard]] static VulkanPipeline Create(VulkanDevice& device, const VulkanPipelineCreateInfo& info);
        ~VulkanPipeline();

        VulkanPipeline(VulkanPipeline&& other) noexcept;
        VulkanPipeline& operator=(VulkanPipeline&& other) noexcept;
        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept { return m_pipeline != VK_NULL_HANDLE; }
        [[nodiscard]] VkPipeline Get() const noexcept { return m_pipeline; }
        [[nodiscard]] VkPipelineLayout GetLayout() const noexcept { return m_layout; }
        [[nodiscard]] std::span<const Slot> GetSlots() const noexcept { return m_slots; }
        /// Vertex buffer slots a draw needs bound: 0 .. count-1.
        [[nodiscard]] uint32_t GetVertexBufferCount() const noexcept { return m_vertexBufferCount; }
        [[nodiscard]] bool UsesScissor() const noexcept { return m_scissor; }

    private:
        void Release() noexcept;

        VkDevice m_device { VK_NULL_HANDLE };
        VkPipeline m_pipeline { VK_NULL_HANDLE };
        VkPipelineLayout m_layout { VK_NULL_HANDLE };
        VkDescriptorSetLayout m_setLayout { VK_NULL_HANDLE };
        Vector<Slot> m_slots;
        uint32_t m_vertexBufferCount { 0 };
        bool m_scissor { false };
    };

} // namespace elm::render::vulkan
