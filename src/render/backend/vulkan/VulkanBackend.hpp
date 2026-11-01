#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/backend/vulkan/VulkanDevice.hpp"
#include "render/backend/vulkan/VulkanFrame.hpp"
#include "render/backend/vulkan/VulkanMemory.hpp"
#include "render/backend/vulkan/VulkanPipeline.hpp"
#include "render/backend/vulkan/VulkanShader.hpp"
#include "render/backend/vulkan/VulkanSwapChain.hpp"
#include "render/rhi/IRenderBackend.hpp"

#include <array>

namespace elm::render::vulkan {

    struct GpuTexture {
        VulkanImage image;
        TextureDesc desc;
    };

    struct GpuBuffer {
        VulkanBuffer buffer;
        BufferDesc desc;
    };

    struct GpuMesh {
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        uint32_t indexCount { 0 };
    };

    /// Native Vulkan 1.3 implementation of the render backend (Linux).
    ///
    /// All calls of a frame (resource commands, Submit, Present) are recorded into one command buffer of the
    /// current VulkanFrame, which is submitted by the first Present. Up to FramesInFlight frames are on the GPU;
    /// destroyed resources are retired to the frame and freed once the GPU is done with it.
    /// Render thread only, Init/Shutdown while it is stopped. Stale handles are ignored.
    class VulkanBackend final : public rhi::IRenderBackend {
    public:
        VulkanBackend();
        ~VulkanBackend() override;

        VulkanBackend(const VulkanBackend&) = delete;
        VulkanBackend& operator=(const VulkanBackend&) = delete;
        VulkanBackend(VulkanBackend&&) = delete;
        VulkanBackend& operator=(VulkanBackend&&) = delete;

        [[nodiscard]] StringView GetName() const override { return "Vulkan"; }
        [[nodiscard]] auto Init(const NativeWindow& window, Size size) -> EngineResult<void> override;
        void Shutdown() override;

        void ExecuteResourceCommands(ResourceCommandList& commands) override;
        [[nodiscard]] bool GetTextureDesc(TextureHandle texture, TextureDesc& desc) const override;

        [[nodiscard]] TextureHandle CreateTransientTexture(const TextureDesc& desc) override;
        void DestroyTransientTexture(TextureHandle texture) override;
        [[nodiscard]] ShaderHandle CreateShader(const ShaderDesc& desc) override;
        [[nodiscard]] PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
        void DestroyPipeline(PipelineHandle pipeline) override;

        void CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height) override;
        void DestroySurface(SurfaceId surface) override;
        void ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height) override;
        [[nodiscard]] bool HasSurface(SurfaceId surface) const override;
        [[nodiscard]] TextureFormat GetSurfaceColorFormat() const override;
        /// Surfaces have no depth buffer: Unknown.
        [[nodiscard]] TextureFormat GetSurfaceDepthFormat() const override;
        void Present(SurfaceId surface) override;

        rhi::SubmitStats Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads) override;

        [[nodiscard]] size_t GetAllocatedMemory() const override;

    private:
        class ResourceExecutor;
        class CommandTranslator;

        /// Render-side handles carry this index bit, so they live in their own registries.
        static constexpr uint32_t TransientIndexBit = 1u << (TextureHandle::IndexBits - 1);

        [[nodiscard]] GpuTexture* FindTexture(TextureHandle handle);
        [[nodiscard]] const GpuTexture* FindTexture(TextureHandle handle) const;
        [[nodiscard]] VulkanSwapChain* FindSurface(SurfaceId surface);
        [[nodiscard]] VulkanImage CreateImage(const TextureDesc& desc);
        [[nodiscard]] VkSampler CreateLinearClampSampler() const;

        /// The frame being recorded; starts the next one when the previous was submitted.
        [[nodiscard]] VulkanFrame& Record();
        /// The frame being recorded or, between frames, the last submitted one: it holds retired resources.
        [[nodiscard]] VulkanFrame& CurrentFrame() noexcept { return m_frames[m_frameSlot]; }
        /// Ends recording and submits, waiting for and signalling the surfaces the frame renders to.
        void SubmitFrame();

        VulkanDevice m_device;
        VkSampler m_linearClampSampler { VK_NULL_HANDLE };
        std::array<VulkanFrame, FramesInFlight> m_frames;
        uint32_t m_frameSlot { 0 };
        bool m_recording { false };

        VulkanSwapChain m_mainSurface;
        UnorderedMap<SurfaceId, UniquePtr<VulkanSwapChain>> m_surfaces;

        // Update-side handles (RenderResources)
        ResourceRegistry<TextureHandle, GpuTexture> m_textures;
        ResourceRegistry<BufferHandle, GpuBuffer> m_buffers;
        ResourceRegistry<MeshHandle, GpuMesh> m_meshes;

        // Render-side handles
        HandleAllocator<TextureHandle> m_transientHandles;
        ResourceRegistry<TextureHandle, GpuTexture> m_transientTextures;
        HandleAllocator<ShaderHandle> m_shaderHandles;
        ResourceRegistry<ShaderHandle, SpirvShader> m_shaders;
        HandleAllocator<PipelineHandle> m_pipelineHandles;
        ResourceRegistry<PipelineHandle, VulkanPipeline> m_pipelines;
    };

} // namespace elm::render::vulkan
