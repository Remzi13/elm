#pragma once

#include "render/backend/vulkan/VulkanBackend.hpp"

#include <array>
#include <optional>

namespace elm::render::vulkan {

    /// GPU copies of the frame's vertex and index upload arenas.
    struct FrameStreams {
        BufferRange vertices;
        BufferRange indices;
    };

    /// Translates one rhi::CommandList into the frame's command buffer (dynamic rendering, push descriptors).
    /// Starts from a clean state, as every list is recorded independently; Finish closes an open render pass.
    ///
    /// Barriers and the layouts textures need for a draw are recorded outside rendering: a pass is suspended
    /// for them and resumed with its contents loaded. Missing resources skip the draw.
    class VulkanBackend::CommandTranslator {
    public:
        CommandTranslator(VulkanBackend& backend, VulkanFrame& frame, const FrameStreams& streams, const rhi::UploadAllocator& uploads) noexcept;

        void Execute(const rhi::cmd::Barrier& command);
        void Execute(const rhi::cmd::BeginRenderPass& command);
        void Execute(const rhi::cmd::EndRenderPass& command);
        void Execute(const rhi::cmd::SetPipeline& command);
        void Execute(const rhi::cmd::SetViewport& command);
        void Execute(const rhi::cmd::SetScissor& command);
        void Execute(const rhi::cmd::BindVertexBuffer& command);
        void Execute(const rhi::cmd::BindIndexBuffer& command);
        void Execute(const rhi::cmd::BindTexture& command);
        void Execute(const rhi::cmd::SetConstants& command);
        void Execute(const rhi::cmd::DrawIndexed& command);
        void Execute(const rhi::cmd::Draw& command);

        void Finish();
        [[nodiscard]] uint32_t GetDrawCount() const noexcept { return m_drawCount; }

    private:
        static constexpr uint32_t MaxVertexSlots = 4;

        struct BoundBuffer {
            VkBuffer buffer { VK_NULL_HANDLE };
            VkDeviceSize offset { 0 };
        };

        /// What the current pass renders to, kept to resume it after a suspension.
        struct RenderTarget {
            VkImageView color { VK_NULL_HANDLE };
            VkImageView depth { VK_NULL_HANDLE };
            bool depthHasStencil { false };
            VkExtent2D extent { 0, 0 };
        };

        /// Values attachments are cleared to when rendering begins; empty ones keep their contents.
        struct AttachmentClears {
            std::optional<VkClearColorValue> color;
            std::optional<float> depth;
        };

        /// Color target of BeginRenderPass in COLOR_ATTACHMENT layout; nullopt when it does not exist.
        [[nodiscard]] std::optional<RenderTarget> PrepareColorTarget(const rhi::ColorAttachment& color);
        void BeginRendering(const AttachmentClears& clears);
        void SuspendRendering();
        void SetFullViewport();
        void ApplyScissor();

        [[nodiscard]] bool PrepareDraw();
        [[nodiscard]] bool PushDescriptors();
        [[nodiscard]] GpuTexture* ResolveTexture();
        [[nodiscard]] BoundBuffer Resolve(const rhi::BufferSource& source) const;
        /// Records image work outside of rendering, resuming the pass afterwards.
        template <typename Work>
        void OutsideRendering(Work&& work);

        VulkanBackend& m_backend;
        VulkanFrame& m_frame;
        const FrameStreams& m_streams;
        const rhi::UploadAllocator& m_uploads;
        VkCommandBuffer m_commands { VK_NULL_HANDLE };

        bool m_passActive { false };
        bool m_rendering { false };
        RenderTarget m_target;
        VkRect2D m_scissor {};
        uint32_t m_drawCount { 0 };

        const VulkanPipeline* m_pipeline { nullptr };
        bool m_descriptorsDirty { true };
        TextureHandle m_texture;
        TextureHandle m_fallbackTexture;
        /// Constants of each pipeline slot, copied into the frame's uniform memory.
        Vector<std::optional<BufferRange>> m_constants;

        std::array<BoundBuffer, MaxVertexSlots> m_vertexBuffers {};
        bool m_vertexBuffersDirty { false };
        BoundBuffer m_indexBuffer;
        uint32_t m_meshIndexCount { 0 };
        bool m_indexBufferDirty { false };
    };

} // namespace elm::render::vulkan
