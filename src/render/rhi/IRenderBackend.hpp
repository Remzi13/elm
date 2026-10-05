#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "math/Primitivs.hpp"

#include "render/api/NativeWindow.hpp"
#include "render/api/PipelineDesc.hpp"
#include "render/api/ResourceCommands.hpp"
#include "render/rhi/CommandList.hpp"
#include "render/rhi/UploadAllocator.hpp"

#include <span>

namespace elm::render::rhi {

    struct SubmitStats {
        uint32_t drawCalls { 0 };
        uint32_t commands { 0 };
    };

    /// Graphics API abstraction. Everything above this interface (passes, graph, RenderSystem) is
    /// backend independent; a backend is selected at build time (ELM_RENDER_BACKEND) by createRenderBackend().
    ///
    /// All methods are called on the render thread, Init/Shutdown while it is stopped.
    class IRenderBackend {
    public:
        virtual ~IRenderBackend() = default;

        [[nodiscard]] virtual StringView GetName() const = 0;
        [[nodiscard]] virtual auto Init(const NativeWindow& window, Size size) -> EngineResult<void> = 0;
        virtual void Shutdown() = 0;

        // --- Resources created by the update side (RenderResources) ---
        virtual void ExecuteResourceCommands(ResourceCommandList& commands) = 0;
        /// Description of an existing texture (update-side or transient). False for unknown handles.
        [[nodiscard]] virtual bool GetTextureDesc(TextureHandle texture, TextureDesc& desc) const = 0;

        // --- Render-side resources ---
        /// Textures owned by the render graph. Their handles never collide with update-side ones.
        [[nodiscard]] virtual TextureHandle CreateTransientTexture(const TextureDesc& desc) = 0;
        virtual void DestroyTransientTexture(TextureHandle texture) = 0;
        [[nodiscard]] virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
        [[nodiscard]] virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
        virtual void DestroyPipeline(PipelineHandle pipeline) = 0;

        // --- Presentation surfaces ---
        virtual void CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height) = 0;
        virtual void DestroySurface(SurfaceId surface) = 0;
        virtual void ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height) = 0;
        [[nodiscard]] virtual bool HasSurface(SurfaceId surface) const = 0;
        [[nodiscard]] virtual TextureFormat GetSurfaceColorFormat() const = 0;
        [[nodiscard]] virtual TextureFormat GetSurfaceDepthFormat() const = 0;
        virtual void Present(SurfaceId surface) = 0;

        // --- Frame ---
        /// Uploads the used part of every arena, then translates the lists in order.
        virtual SubmitStats Submit(std::span<const CommandList* const> lists, const UploadAllocator& uploads) = 0;

        [[nodiscard]] virtual size_t GetAllocatedMemory() const = 0;
    };

    [[nodiscard]] UniquePtr<IRenderBackend> createRenderBackend();

} // namespace elm::render::rhi
