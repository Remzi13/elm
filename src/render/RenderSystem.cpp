#include "render/RenderSystem.hpp"
#include "render/OverlayRenderPass.hpp"
#include "render/RenderResourceProvider.hpp"
#include "render/SceneRenderPass.hpp"
#include "render/TextureManager.hpp"
#include "render/BufferManager.hpp"

#include "core/Log.hpp"

#include "core/Profiling.hpp"

// Diligent Engine Includes
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/PipelineResourceSignature.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceVariable.h"
#include "Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"

#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"


#include "graphics/MeshDataStorage.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <utility>

#include "render/backends/Utils.hpp"

#if PLATFORM_WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace elm {

namespace {

    std::unordered_map<GLFWwindow*, RenderSystem*> g_renderSystemsByWindow;

    class ResourceCommandExecutor {
    public:
        ResourceCommandExecutor(render::RenderResourceProvider& resourceProvider,
            render::MeshManager& meshManager, render::BufferManager& bufferManager)
            : m_resourceProvider(resourceProvider)
            , m_meshManager(meshManager)
            , m_bufferManager(bufferManager)
        {
        }

        void Execute(render::command::resource::CreateTexture& command)
        {
            (void)m_resourceProvider.CreateTexture(command.handler, command.info);
        }

        void Execute(render::command::resource::UploadTexture command)
        {
            m_resourceProvider.UpdateTexture(command.handler, command.data);
        }

        void Execute(render::command::resource::DestroyTexture& command)
        {
            m_resourceProvider.ReleaseTexture(command.handler);
        }

        void Execute(render::command::resource::CreateMesh& command)
        {
            using namespace render;
            Mesh mesh = m_meshManager.GetMeshByData(command.meshData);
            if (!mesh.ib.IsValid() || !mesh.vb.IsValid()) {
                const auto& meshData = getMeshData(command.meshData);

                mesh.vb = m_bufferManager.CreateBuffer(BufferInfo { "Mesh VB", BufferType::VertexBuffer, meshData.vertices.size() * sizeof(Vertex), (void*)meshData.vertices.data() });
                mesh.ib = m_bufferManager.CreateBuffer(BufferInfo { "Mesh IB", BufferType::IndexBuffer, meshData.indices.size() * sizeof(uint32_t), (void*)meshData.indices.data() });

                mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
            }
            m_meshManager.PushMesh(command.handler, command.meshData, mesh);
        }

        void Execute(render::command::resource::DestroyMesh& command)
        {
            render::Mesh mesh;
            m_meshManager.PopMesh(command.handler, mesh);
            if (mesh.ib.IsValid() && mesh.vb.IsValid()) {
                m_bufferManager.DestroyBuffer(mesh.ib);
                m_bufferManager.DestroyBuffer(mesh.vb);
            }
        }

    private:
        render::RenderResourceProvider& m_resourceProvider;
        render::BufferManager& m_bufferManager;
        render::MeshManager& m_meshManager;
    };

    class DilligentAllocator : public Diligent::IMemoryAllocator {
    public:
        virtual ~DilligentAllocator() = default;

        struct AllocationHeader {
            void* rawPointer;
            size_t requestedSize;
            size_t allocatedSize;
        };

        virtual void* Allocate(size_t Size, [[maybe_unused]] const char* DebugDesc, [[maybe_unused]] const char* File, [[maybe_unused]] int Line) override
        {
            constexpr size_t Alignment = 64;
            size_t allocatedSize = Size + Alignment + sizeof(AllocationHeader);
            void* rawPointer = memory::allocate_impl(allocatedSize);
            void* ptr = static_cast<char*>(rawPointer) + sizeof(AllocationHeader);
            size_t availableSize = allocatedSize - sizeof(AllocationHeader);
            std::align(Alignment, Size, ptr, availableSize);
            if (!ptr)
                return nullptr;

            auto* header = reinterpret_cast<AllocationHeader*>(ptr) - 1;
            header->rawPointer = rawPointer;
            header->requestedSize = Size;
            header->allocatedSize = allocatedSize;
            m_TotalAllocated.fetch_add(Size, std::memory_order_relaxed);

            return ptr;
        }

        virtual void Free(void* Ptr) override
        {
            if (!Ptr)
                return;

            auto* header = reinterpret_cast<AllocationHeader*>(Ptr) - 1;
            void* rawPointer = header->rawPointer;
            const size_t requestedSize = header->requestedSize;
            const size_t allocatedSize = header->allocatedSize;

            m_TotalAllocated.fetch_sub(requestedSize, std::memory_order_relaxed);
            memory::deallocate_impl(rawPointer, allocatedSize);
        }

        size_t GetTotalAllocatedBytes() const
        {
            return m_TotalAllocated.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<size_t> m_TotalAllocated { 0 };
    } g_Allocator;
}

class RenderSystem::RenderCommandExecutor : public ResourceCommandExecutor {
public:
    explicit RenderCommandExecutor(RenderSystem& renderSystem)
        : ResourceCommandExecutor(*renderSystem.m_resourceProvider,
            renderSystem.m_meshManager, renderSystem.m_bufferManager)
        , m_renderSystem(renderSystem)
    {
    }

    using ResourceCommandExecutor::Execute;

    void Execute(render::command::resource::ResizeMainSwapChain& command)
    {
        if (command.width > 0 && command.height > 0)
            m_renderSystem.ApplyMainSwapChainResize(command.width, command.height);
    }

private:
    RenderSystem& m_renderSystem;
};

// Shaders are loaded from shaders/ at runtime. This keeps shader editing independent
// from the executable and also allows the same source to be replaced without a rebuild.
static String LoadShaderSource(const char* fileName)
{
    const std::filesystem::path sourceRoot = std::filesystem::path { __FILE__ }.parent_path().parent_path().parent_path();
    const std::filesystem::path paths[] = {
        std::filesystem::current_path() / "shaders" / fileName,
        std::filesystem::current_path() / "assets/shaders" / fileName,
        sourceRoot / "shaders" / fileName,
        sourceRoot / "assets/shaders" / fileName
    };
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            continue;

        const auto size = file.tellg();
        if (size <= 0 || !file.seekg(0))
            continue;

        String source(static_cast<size_t>(size), '\0');
        if (file.read(source.data(), size)) {
            LOG_MESSAGE(log::Category::Render, "Shaders", "Loaded shader: %s", path.string().c_str());            
            return source;
        }
    }

    ERORR_MESSAGE(log::Category::Render, "Shaders", "Cannot load shader from the working directory or project root : %s", fileName);
    return { };
}

RenderSystem::RenderSystem() = default;

RenderSystem::~RenderSystem()
{
    Shutdown();
}

auto RenderSystem::Init(Size size, StringView title) -> EngineResult<void>
{
    if (m_initialized) {
        return { };
    }

    m_size = size;    

#if PLATFORM_WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    if (!glfwInit()) {
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to initialize GLFW"));
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(m_size.width), static_cast<int>(m_size.height), title.data(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to create GLFW window"));
    }

    glfwSetFramebufferSizeCallback(m_window, &RenderSystem::OnFramebufferSizeChanged);
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0) {
        m_size.width = static_cast<uint32_t>(framebufferWidth);
        m_size.height = static_cast<uint32_t>(framebufferHeight);
    }

#if PLATFORM_WIN32
    auto* pFactory = Diligent::GetEngineFactoryD3D12();
    if (!pFactory) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryD3D12"));
    }

    Diligent::EngineD3D12CreateInfo engineCreateInfo;
    engineCreateInfo.NumDeferredContexts = 0;
    // engineCreateInfo.DynamicHeapSize = 128 << 20;
    engineCreateInfo.DynamicHeapPageSize = 8 << 20;
    engineCreateInfo.pRawMemAllocator = &g_Allocator;
    pFactory->CreateDeviceAndContextsD3D12(engineCreateInfo, &m_renderDevice, &m_deviceContext);
#else
    auto* pFactory = Diligent::GetEngineFactoryVk();
    if (!pFactory) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryVk"));
    }

    Diligent::EngineVkCreateInfo engineCreateInfo;
    engineCreateInfo.NumDeferredContexts = 0;
    engineCreateInfo.DynamicHeapSize = 128 << 20;
    engineCreateInfo.DynamicHeapPageSize = 8 << 20;
    engineCreateInfo.pRawMemAllocator = &g_Allocator;
    pFactory->CreateDeviceAndContextsVk(engineCreateInfo, &m_renderDevice, &m_deviceContext);
#endif

    if (!m_renderDevice || !m_deviceContext) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent Render Device & Contexts"));
    }

    void* nativeHandle = nullptr;
    void* nativeDisplay = nullptr;
#if PLATFORM_WIN32
    nativeHandle = glfwGetWin32Window(m_window);
#else
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        nativeDisplay = glfwGetWaylandDisplay();
        nativeHandle = glfwGetWaylandWindow(m_window);
    } else {
        nativeDisplay = glfwGetX11Display();
        nativeHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(m_window)));
    }
#endif

    m_resourceProvider = MakeUnique<render::RenderResourceProvider>(m_renderDevice, m_deviceContext, m_textureStore);
    m_swapChain = CreateSwapChain(m_size.width, m_size.height, nativeHandle, nativeDisplay);
    if (!m_swapChain || !m_swapChain.GetCurrentBackBufferRTV() || !m_swapChain.GetDepthBufferDSV()) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed,
            "Failed to create Diligent SwapChain with required color and depth-stencil views"));
    }
    m_resourceProvider->SetMainSwapChain(&m_swapChain);

    if (!m_bufferManager.Init(m_renderDevice, m_deviceContext))
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Buffer Manager "));

    if (!m_meshManager.Init(&m_deferredCommandQueue))
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Mesh Manager "));

    m_dynamicInstanceBuffer.Init(m_renderDevice, "Dynamic Instance Linear Allocator", render::BufferType::VertexBuffer, 16 * 1024 * 1024);
    m_dynamicUniformBuffer.Init(m_renderDevice, "Dynamic Uniform Linear Allocator", render::BufferType::UniformBuffer, 2 * 1024 * 1024);

    // Initialize 3D Rendering Pipeline
    InitPipeline();

    g_renderSystemsByWindow[m_window] = this;
    m_initialized = true;
    LOG_MESSAGE(log::Category::Render, "RenderSystem", "Diligent Engine, 3D Mesh Pipeline, and SOC Testbed initialized.");
    return { };
}

void RenderSystem::InitPipeline()
{
    auto scenePass = MakeUnique<render::SceneRenderPass>();
    scenePass->Initialize(m_renderDevice,
        m_dynamicUniformBuffer.GetBuffer(),
        m_swapChain.GetDesc().ColorBufferFormat,
        m_swapChain.GetDesc().DepthBufferFormat);
    m_renderGraph.push_back(std::move(scenePass));

    auto overlayPass = MakeUnique<render::OverlayRenderPass>();
    overlayPass->Initialize(m_renderDevice, m_swapChain.GetDesc().ColorBufferFormat);
    m_renderGraph.push_back(std::move(overlayPass));
}

void RenderSystem::OnFramebufferSizeChanged(GLFWwindow* window, int width, int height)
{
    const auto it = g_renderSystemsByWindow.find(window);
    if (it == g_renderSystemsByWindow.end())
        return;
    auto* system = it->second;

    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    system->m_size.width = static_cast<uint32_t>((std::max)(windowWidth, 0));
    system->m_size.height = static_cast<uint32_t>((std::max)(windowHeight, 0));
    if (width > 0 && height > 0)
        system->m_deferredCommandQueue.Push(render::command::resource::ResizeMainSwapChain {
            static_cast<uint32_t>(width), static_cast<uint32_t>(height)
        });
}

void RenderSystem::ApplyMainSwapChainResize(uint32_t width, uint32_t height)
{
    if (!m_swapChain)
        return;

    m_swapChain.ResizeIfNeeded(width, height);
}

bool RenderSystem::ShouldClose() const
{
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

render::SwapChain RenderSystem::CreateSwapChain(uint32_t width, uint32_t height, void* nativeHandle,
    void* nativeDisplay, bool withDepthBuffer)
{
    return m_resourceProvider
        ? m_resourceProvider->CreateSwapChain(width, height, nativeHandle, nativeDisplay, withDepthBuffer)
        : render::SwapChain{};
}

render::RenderPassContext RenderSystem::CreatePassContext()
{
    return {
        m_deviceContext,
        m_resourceProvider.get(),
        &m_bufferManager,
        &m_meshManager,
        &m_dynamicInstanceBuffer,
        &m_dynamicUniformBuffer,
        &m_swapChain
    };
}

void RenderSystem::CommitCommands()
{
    m_deferredCommandQueue.CommitFrame();
}

void RenderSystem::ExecuteRenderGraph(FrameData& frameData, render::OverlayFrame& overlay)
{
    auto passContext = CreatePassContext();
    render::RenderFrameContext frameContext {
        passContext, frameData, overlay.viewPort, overlay
    };
    for (const auto& pass : m_renderGraph)
        pass->Execute(frameContext);
}

void RenderSystem::FlushCommands()
{
    if (!m_deviceContext)
        return;

    m_deferredCommandQueue.CommitFrame();
    m_deferredCommandQueue.BeginFrame();
    RenderCommandExecutor executor(*this);
    m_deferredCommandQueue.Execute(executor);
}

void RenderSystem::BeginFrame()
{
    if (!m_swapChain || !m_deviceContext)
        return;

    m_deferredCommandQueue.BeginFrame();
    RenderCommandExecutor executor(*this);
    m_deferredCommandQueue.Execute(executor);

    auto* pRTV = m_swapChain.GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain.GetDepthBufferDSV();

    const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
    m_deviceContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

uint64_t RenderSystem::RenderFrame(FrameData& frameData, render::OverlayFrame& overlay)
{
    ExecuteRenderGraph(frameData, overlay);
    return ++m_renderedFrameNumber;
}

void RenderSystem::EndFrame()
{
    if (!m_swapChain || !m_deviceContext)
        return;
    ELM_PROFILE_SCOPE_N("Present");
    m_swapChain.Present();
}

void RenderSystem::Shutdown()
{
    if (!m_initialized)
        return;

    for (const auto& pass : m_renderGraph)
        pass->ReleaseResources();

    // Render thread is stopped at this point: execute what is still queued (e.g. texture releases)
    FlushCommands();
    m_renderGraph.clear();

    m_resourceProvider->ClearTextures();
    m_resourceProvider.reset();

    m_bufferManager.Clear();

    m_dynamicInstanceBuffer.Release();
    m_dynamicUniformBuffer.Release();

    m_swapChain.Reset();
    if (m_deviceContext) {
        m_deviceContext->Release();
        m_deviceContext = nullptr;
    }
    if (m_renderDevice) {
        m_renderDevice->Release();
        m_renderDevice = nullptr;
    }

    if (m_window) {
        g_renderSystemsByWindow.erase(m_window);
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();

    m_initialized = false;
    LOG_MESSAGE(log::Category::Render, "RenderSystem", "Shutdown completed.");
}

size_t RenderSystem::GetMemAllocated() const
{
    return g_Allocator.GetTotalAllocatedBytes();
}

} // namespace Engine
