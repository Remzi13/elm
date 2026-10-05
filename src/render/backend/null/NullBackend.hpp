#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/rhi/IRenderBackend.hpp"

namespace elm::render::null {

    /// Backend without a GPU: keeps resource bookkeeping, validates command lists and draws nothing.
    /// Proves that everything above rhi::IRenderBackend builds and runs without Diligent, and is
    /// useful for headless runs and tests (ELM_RENDER_BACKEND=Null).
    class NullBackend final : public rhi::IRenderBackend {
    public:
        [[nodiscard]] StringView GetName() const override { return "Null"; }
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
        [[nodiscard]] TextureFormat GetSurfaceColorFormat() const override { return TextureFormat::RGBA8_UNORM_SRGB; }
        [[nodiscard]] TextureFormat GetSurfaceDepthFormat() const override { return TextureFormat::D32_FLOAT; }
        void Present(SurfaceId) override { }

        rhi::SubmitStats Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads) override;

        [[nodiscard]] size_t GetAllocatedMemory() const override { return 0; }

    private:
        class ResourceExecutor;

        static constexpr uint32_t TransientIndexBit = 1u << (TextureHandle::IndexBits - 1);

        ResourceRegistry<TextureHandle, TextureDesc> m_textures;
        ResourceRegistry<MeshHandle, uint32_t> m_meshes;
        HandleAllocator<TextureHandle> m_transientHandles;
        ResourceRegistry<TextureHandle, TextureDesc> m_transientTextures;
        HandleAllocator<ShaderHandle> m_shaders;
        HandleAllocator<PipelineHandle> m_pipelines;
        UnorderedMap<SurfaceId, Size> m_surfaces;
    };

} // namespace elm::render::null
