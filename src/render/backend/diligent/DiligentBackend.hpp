#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/backend/diligent/SwapChain.hpp"
#include "render/rhi/IRenderBackend.hpp"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"

namespace elm::render::diligent {

    struct GpuTexture {
        Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
        TextureDesc desc;
    };

    struct GpuBuffer {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
        BufferDesc desc;
    };

    struct GpuMesh {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> vertexBuffer;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> indexBuffer;
        uint32_t indexCount { 0 };
    };

    struct GpuShader {
        Diligent::RefCntAutoPtr<Diligent::IShader> shader;
    };

    struct GpuPipeline {
        struct ConstantSlot {
            uint32_t slot { 0 };
            Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
        };
        struct TextureSlot {
            uint32_t slot { 0 };
            Diligent::SHADER_TYPE stage { Diligent::SHADER_TYPE_PIXEL };
            String name;
        };
        struct Binding {
            Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
            // Kept alive by the SRB, so a recreated texture under the same handle is detected by address
            Diligent::ITexture* texture { nullptr };
        };

        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
        Vector<ConstantSlot> constants;
        /// At most one mutable texture per pipeline; a binding is cached per bound texture.
        Vector<TextureSlot> textures;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> defaultBinding;
        UnorderedMap<TextureHandle, Binding> bindings;
    };

    /// Diligent Engine implementation (D3D12 on Windows, Vulkan on Linux) of the render backend.
    class DiligentBackend final : public rhi::IRenderBackend {
    public:
        DiligentBackend();
        ~DiligentBackend() override;

        DiligentBackend(const DiligentBackend&) = delete;
        DiligentBackend& operator=(const DiligentBackend&) = delete;

        [[nodiscard]] StringView GetName() const override { return "Diligent"; }
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
        [[nodiscard]] SwapChain* FindSurface(SurfaceId surface);
        [[nodiscard]] Diligent::RefCntAutoPtr<Diligent::ITexture> CreateNativeTexture(const TextureDesc& desc);
        [[nodiscard]] SwapChain CreateSwapChain(const NativeWindow& window, uint32_t width, uint32_t height, bool withDepth);
        void UploadArena(rhi::UploadArena arena, const rhi::UploadAllocator& uploads);

        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> m_device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> m_context;
        SwapChain m_mainSurface;
        UnorderedMap<SurfaceId, SwapChain> m_surfaces;

        // Update-side handles (RenderResources)
        ResourceRegistry<TextureHandle, GpuTexture> m_textures;
        ResourceRegistry<BufferHandle, GpuBuffer> m_buffers;
        ResourceRegistry<MeshHandle, GpuMesh> m_meshes;

        // Render-side handles
        HandleAllocator<TextureHandle> m_transientHandles;
        ResourceRegistry<TextureHandle, GpuTexture> m_transientTextures;
        HandleAllocator<ShaderHandle> m_shaderHandles;
        ResourceRegistry<ShaderHandle, GpuShader> m_shaders;
        HandleAllocator<PipelineHandle> m_pipelineHandles;
        ResourceRegistry<PipelineHandle, GpuPipeline> m_pipelines;

        /// GPU copies of the per-frame upload arenas (vertex and index streams).
        Diligent::RefCntAutoPtr<Diligent::IBuffer> m_uploadBuffers[2];
        bool m_uploadMapped[2] { false, false };
    };

} // namespace elm::render::diligent
