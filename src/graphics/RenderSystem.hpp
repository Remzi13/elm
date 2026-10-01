#pragma once

#include "core/Error.hpp"

#include "graphics/Camera.hpp"

#include "Scene/TestScenes.hpp"
#include "Scene/Transform.hpp"

#include "graphics/render/BufferManager.hpp"
#include "graphics/render/DynamicLinearAllocator.hpp"
#include "graphics/render/MeshManager.h"
#include "graphics/render/SwapChain.hpp"
#include "graphics/render/TextureStore.hpp"

#include "graphics/render/CommandQueue.hpp"

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

    [[nodiscard]] auto Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void>;
    [[nodiscard]] bool ShouldClose() const;
    [[nodiscard]] render::RenderResourceProvider& GetResourceProvider() noexcept;
    [[nodiscard]] render::SwapChain CreateSwapChain(uint32_t width, uint32_t height, void* nativeHandle,
        void* nativeDisplay, bool withDepthBuffer = true);
    [[nodiscard]] render::CommandQueue& GetCommandQueue() noexcept { return m_commandQueue; }
    void InitializeEngineViewportTexture(core::Handler textureHandler);

    // --- Main thread ---
    void CommitCommands();
    // Executes queued work on the calling thread. Only when the render thread is stopped
    void FlushCommands();

    // --- Render thread ---
    void BeginFrame();
    void Draw(FrameData& frameData);
    void EndFrame();

    void Shutdown();

    // --- Any thread: immutable after Init or atomic ---
    [[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }
    [[nodiscard]] core::Handler GetEngineViewportTexture() const { return m_engineViewportTexture; }
    [[nodiscard]] uint32_t GetEngineViewportWidth() const { return m_engineViewportWidth; }
    [[nodiscard]] uint32_t GetEngineViewportHeight() const { return m_engineViewportHeight; }
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] size_t GetMemAllocated() const;

private:
    void InitPipeline();
    void CreateEngineViewport(uint32_t width, uint32_t height);

    void Draw(const UnorderedMap<core::Handler, Vector<RenderObject>>& objects);

private:
    GLFWwindow* m_window { nullptr };

    // Diligent Engine components
    Diligent::IRenderDevice* m_renderDevice { nullptr };
    Diligent::IDeviceContext* m_deviceContext { nullptr };
    render::SwapChain m_swapChain;

    // Shaders & Pipelines
    Diligent::IPipelineState* m_pPSO { nullptr };
    Diligent::IPipelineState* m_pHighlightPSO { nullptr };
    Diligent::IShaderResourceBinding* m_pSRB { nullptr };
        
    // Dynamic Instance Buffer
    static constexpr size_t MaxInstances = 30000;

    // Offscreen render target displayed inside the dockspace.
    Diligent::ITexture* m_pEngineViewportTex { nullptr };
    Diligent::ITextureView* m_pEngineViewportRTV { nullptr };
    Diligent::ITextureView* m_pEngineViewportDSV { nullptr };
    Diligent::ITextureView* m_pEngineViewportSRV { nullptr };
    core::Handler m_engineViewportTexture;
    uint32_t m_engineViewportWidth { 1280 };
    uint32_t m_engineViewportHeight { 720 };
    bool m_engineViewportIsShaderResource { false };

    render::BufferManager m_bufferManager;    
    render::TextureStore m_textureStore;
    render::MeshManager m_meshManager;
    render::DynamicLinearAllocator m_dynamicInstanceBuffer;
    render::DynamicLinearAllocator m_dynamicUniformBuffer;

    uint32_t m_width { 1280 };
    uint32_t m_height { 720 };
    bool m_initialized { false };

    render::CommandQueue m_commandQueue;
    UniquePtr<render::RenderResourceProvider> m_resourceProvider;

};

} // namespace Engine
