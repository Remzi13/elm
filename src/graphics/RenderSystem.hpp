#pragma once

#include "core/Error.hpp"

#include "graphics/Camera.hpp"

#include "Scene/TestScenes.hpp"
#include "Scene/Transform.hpp"

#include "graphics/render/BufferManager.hpp"
#include "graphics/render/DynamicLinearAllocator.hpp"
#include "graphics/render/MeshManager.h"
#include "graphics/render/SwapChain.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/TextureStore.hpp"

#include "graphics/render/CommandQueue.hpp"
#include "graphics/render/ResourceView.hpp"

#include "graphics/render/IRenderPass.hpp"

#include <atomic>
#include <unordered_map>

struct GLFWwindow;

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct IPipelineState;
struct IShaderResourceBinding;
struct ITexture;
struct ITextureView;
}

namespace elm {

namespace render {
class RenderResourceProvider;
}

struct FrameStats {
    float fps { 0.0f };
    float deltaTimeMs { 0.0f };
    uint32_t physicsBodyCount { 0 };
    Transform boxTransform;
    Transform groundTransform;
};

struct RenderObject {
    Matrix4x4 transform;
    Vector4 color;
};

struct FrameData {
    Camera camera;
    UnorderedMap<core::Handler, Vector<RenderObject>> objects;
};

class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) noexcept = delete;
    RenderSystem& operator=(RenderSystem&&) noexcept = delete;

    [[nodiscard]] auto Init(Size size, StringView title) -> EngineResult<void>;
    [[nodiscard]] bool ShouldClose() const;
    /// Cross-thread mailbox for deferred backend operations (resource creation,
    /// destruction, and resizing). It does not contain the per-frame pass schedule.
    /// The producer must call CommitCommands() after recording a frame's operations.
    [[nodiscard]] render::CommandQueue& GetCommandQueue() noexcept { return m_deferredCommandQueue; }
    // --- Update/producer thread ---
    void CommitCommands();
    // Executes queued work on the calling thread. Only when the render thread is stopped
    void FlushCommands();

    // --- Render thread ---
    // BeginFrame consumes deferred cross-thread work; RenderFrame executes the render graph
    // and returns its 1-based frame number; EndFrame presents after all graph passes complete.
    void BeginFrame();
    [[nodiscard]] uint64_t RenderFrame(FrameData& frameData, render::OverlayFrame& overlay);
    void EndFrame();

    void Shutdown();

    // --- Main thread ---
    [[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }
    [[nodiscard]] Size GetSize() const { return m_size; }    
    [[nodiscard]] size_t GetMemAllocated() const;

private:
    class RenderCommandExecutor;

    void InitPipeline();
    [[nodiscard]] render::SwapChain CreateSwapChain(uint32_t width, uint32_t height, void* nativeHandle,
        void* nativeDisplay, bool withDepthBuffer = true);
    void ApplyMainSwapChainResize(uint32_t width, uint32_t height);
    static void OnFramebufferSizeChanged(GLFWwindow* window, int width, int height);

    void ExecuteRenderGraph(FrameData& frameData, render::OverlayFrame& overlay);

    /// Builds the shared resource view passed to render passes.
    [[nodiscard]] render::RenderPassContext CreatePassContext();

private:
    GLFWwindow* m_window { nullptr };

    // Diligent Engine components
    Diligent::IRenderDevice* m_renderDevice { nullptr };
    Diligent::IDeviceContext* m_deviceContext { nullptr };
    render::SwapChain m_swapChain;

    // Dynamic Instance Buffer
    static constexpr size_t MaxInstances = 30000;

    render::BufferManager m_bufferManager;    
    render::TextureStore m_textureStore;
    render::MeshManager m_meshManager;
    render::DynamicLinearAllocator m_dynamicInstanceBuffer;
    render::DynamicLinearAllocator m_dynamicUniformBuffer;

    Size m_size { 1280, 720 };    
    bool m_initialized { false };
    uint64_t m_renderedFrameNumber { 0 };

    render::CommandQueue m_deferredCommandQueue;
    UniquePtr<render::RenderResourceProvider> m_resourceProvider;

    // Render graph owns pass lifetimes and defines execution order.
    Vector<UniquePtr<render::IRenderPass>> m_renderGraph;
};

} // namespace Engine
