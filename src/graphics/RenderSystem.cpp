#include "graphics/RenderSystem.hpp"
#include "graphics/render/RenderResourceProvider.hpp"
#include "graphics/render/TextureManager.hpp"

#include "core/Log.hpp"

#include "core/Profiling.hpp"

// Diligent Engine Includes
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/PipelineResourceSignature.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"

#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "graphics/render/BufferManager.hpp"

#include "graphics/MeshDataStorage.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <utility>

// TODO it is need ?
#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "graphics/render/backends/Utils.hpp"

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

        void Execute(render::command::CreateTexture& command)
        {
            (void)m_resourceProvider.CreateTexture(command.handler, command.info);
        }

        void Execute(render::command::UploadTexture command)
        {
            m_resourceProvider.UpdateTexture(command.handler, command.data);
        }

        void Execute(render::command::DestroyTexture& command)
        {
            m_resourceProvider.ReleaseTexture(command.handler);
        }

        void Execute(render::command::CreateMesh& command)
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

        void Execute(render::command::DestroyMesh& command)
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

class RenderSystem::Executor : public ResourceCommandExecutor {
public:
    explicit Executor(RenderSystem& renderSystem)
        : ResourceCommandExecutor(*renderSystem.m_resourceProvider,
            renderSystem.m_meshManager, renderSystem.m_bufferManager)
        , m_renderSystem(renderSystem)
    {
    }

    using ResourceCommandExecutor::Execute;

    void Execute(render::command::ResizeMainSwapChain& command)
    {
        if (command.width > 0 && command.height > 0)
            m_renderSystem.ApplyMainSwapChainResize(command.width, command.height);
    }

    void Execute(render::command::ResizeEngineViewport& command)
    {
        if (command.width > 0 && command.height > 0)
            m_renderSystem.CreateEngineViewport(command.width, command.height);
    }

    void Execute(render::command::CreateRenderSurface& command)
    {
        m_renderSystem.CreateRenderSurface(command);
    }

    void Execute(render::command::ResizeRenderSurface& command)
    {
        m_renderSystem.ResizeRenderSurface(command);
    }

    void Execute(render::command::DestroyRenderSurface& command)
    {
        m_renderSystem.DestroyRenderSurface(command);
    }

    void Execute(render::command::BeginRenderPass& command)
    {
        m_renderSystem.BeginRenderPass(command);
    }

    void Execute(render::command::DrawIndexed& command)
    {
        m_renderSystem.DrawIndexed(command);
    }

    void Execute(render::command::EndRenderPass& command)
    {
        m_renderSystem.EndRenderPass(command);
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

    if (!m_meshManager.Init(&m_commandQueue))
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
    // Create Shaders
    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

    const String VSSource = LoadShaderSource("mesh.vert.hlsl");
    const String PSSource = LoadShaderSource("mesh.frag.hlsl");
    const String PSHighlightSource = LoadShaderSource("highlight.frag.hlsl");
    if (VSSource.empty() || PSSource.empty() || PSHighlightSource.empty()) {
        std::cerr << "[RenderSystem] Pipeline creation aborted: shader source is missing." << std::endl;
        return;
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name = "Mesh VS";
        ShaderCI.Source = VSSource.c_str();
        m_renderDevice->CreateShader(ShaderCI, &pVS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Mesh PS";
        ShaderCI.Source = PSSource.c_str();
        m_renderDevice->CreateShader(ShaderCI, &pPS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pHighlightPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name = "Highlight PS";
        ShaderCI.Source = PSHighlightSource.c_str();
        m_renderDevice->CreateShader(ShaderCI, &pHighlightPS);
    }

    if (!pVS || !pPS || !pHighlightPS) {
        std::cerr << "[RenderSystem] Pipeline creation aborted: shader compilation failed." << std::endl;
        return;
    }

    // Input Layout
    Diligent::LayoutElement LayoutElems[] = {
        // Slot 0: Per-vertex
        Diligent::LayoutElement { 0, 0, 3, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 1, 0, 3, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 2, 0, 2, Diligent::VT_FLOAT32, false },

        // Slot 1: Per-instance (Transform Matrix + Color)
        Diligent::LayoutElement { 3, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 4, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 5, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 6, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 7, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE }
    };

    // Main Opaque Pipeline State
    Diligent::GraphicsPipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Opaque Mesh PSO";
    auto& Pipeline = PSOCI.GraphicsPipeline;
    Pipeline.NumRenderTargets = 1;
    Pipeline.RTVFormats[0] = m_swapChain.GetDesc().ColorBufferFormat;
    Pipeline.DSVFormat = m_swapChain.GetDesc().DepthBufferFormat;
    Pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Pipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    Pipeline.DepthStencilDesc.DepthEnable = true;
    Pipeline.DepthStencilDesc.DepthWriteEnable = true;

    Pipeline.InputLayout.LayoutElements = LayoutElems;
    Pipeline.InputLayout.NumElements = _countof(LayoutElems);

    PSOCI.pVS = pVS;
    PSOCI.pPS = pPS;

    PSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    m_renderDevice->CreateGraphicsPipelineState(PSOCI, &m_pPSO);
    if (m_pPSO) {
        auto* pVar = m_pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
        if (pVar)
            pVar->Set(m_dynamicUniformBuffer.GetBuffer());
        m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
    }

    // Highlight (Culled Visualizer) Pipeline State
    Diligent::GraphicsPipelineStateCreateInfo HighlightPSOCI = PSOCI;
    HighlightPSOCI.PSODesc.Name = "Highlight Culled PSO";
    HighlightPSOCI.pPS = pHighlightPS;
    HighlightPSOCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    auto& Blend0 = HighlightPSOCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
    Blend0.BlendEnable = true;
    Blend0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    Blend0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    Blend0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    Blend0.DestBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
    m_renderDevice->CreateGraphicsPipelineState(HighlightPSOCI, &m_pHighlightPSO);

    const String OverlayVSSource = LoadShaderSource("overlay.vert.hlsl");
    const String OverlayPSSource = LoadShaderSource("overlay.frag.hlsl");
    if (OverlayVSSource.empty() || OverlayPSSource.empty())
        return;

    Diligent::RefCntAutoPtr<Diligent::IShader> pOverlayVS;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.Desc.Name = "Overlay VS";
    ShaderCI.Source = OverlayVSSource.c_str();
    m_renderDevice->CreateShader(ShaderCI, &pOverlayVS);

    Diligent::RefCntAutoPtr<Diligent::IShader> pOverlayPS;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ShaderCI.Desc.Name = "Overlay PS";
    ShaderCI.Source = OverlayPSSource.c_str();
    const Diligent::ShaderMacro OverlayGammaMacro[] {
        { "OVERLAY_MANUAL_SRGB", "1" }
    };
    if (Diligent::GetTextureFormatAttribs(m_swapChain.GetDesc().ColorBufferFormat).ComponentType ==
        Diligent::COMPONENT_TYPE_UNORM_SRGB) {
        ShaderCI.Macros = { OverlayGammaMacro, _countof(OverlayGammaMacro) };
    } else {
        ShaderCI.Macros = {};
    }
    m_renderDevice->CreateShader(ShaderCI, &pOverlayPS);
    if (!pOverlayVS || !pOverlayPS)
        return;

    Diligent::LayoutElement OverlayLayout[] = {
        Diligent::LayoutElement { 0, 0, 2, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 1, 0, 2, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 2, 0, 4, Diligent::VT_FLOAT32, false }
    };
    Diligent::GraphicsPipelineStateCreateInfo OverlayPSOCI;
    OverlayPSOCI.PSODesc.Name = "Overlay PSO";
    auto& OverlayPipeline = OverlayPSOCI.GraphicsPipeline;
    OverlayPipeline.NumRenderTargets = 1;
    OverlayPipeline.RTVFormats[0] = m_swapChain.GetDesc().ColorBufferFormat;
    OverlayPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    OverlayPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    OverlayPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    OverlayPipeline.RasterizerDesc.ScissorEnable = true;
    OverlayPipeline.DepthStencilDesc.DepthEnable = false;
    OverlayPipeline.InputLayout.LayoutElements = OverlayLayout;
    OverlayPipeline.InputLayout.NumElements = _countof(OverlayLayout);
    auto& OverlayBlend = OverlayPipeline.BlendDesc.RenderTargets[0];
    OverlayBlend.BlendEnable = true;
    OverlayBlend.SrcBlend = Diligent::BLEND_FACTOR_ONE;
    OverlayBlend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    OverlayBlend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    OverlayBlend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    OverlayPSOCI.pVS = pOverlayVS;
    OverlayPSOCI.pPS = pOverlayPS;

    Diligent::ShaderResourceVariableDesc OverlayVariable {
        Diligent::SHADER_TYPE_PIXEL,
        "g_Texture",
        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE
    };
    OverlayPSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    OverlayPSOCI.PSODesc.ResourceLayout.Variables = &OverlayVariable;
    OverlayPSOCI.PSODesc.ResourceLayout.NumVariables = 1;
    Diligent::ImmutableSamplerDesc OverlaySampler;
    OverlaySampler.ShaderStages = Diligent::SHADER_TYPE_PIXEL;
    OverlaySampler.SamplerOrTextureName = "g_Texture_sampler";
    OverlaySampler.Desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    OverlaySampler.Desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    OverlaySampler.Desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    OverlaySampler.Desc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    OverlaySampler.Desc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    OverlaySampler.Desc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    OverlayPSOCI.PSODesc.ResourceLayout.ImmutableSamplers = &OverlaySampler;
    OverlayPSOCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    m_renderDevice->CreateGraphicsPipelineState(OverlayPSOCI, &m_pOverlayPSO);
}

void RenderSystem::InitializeEngineViewportTexture(const render::TextureInfo& colorTextureInfo,
    const render::TextureInfo& depthTextureInfo,
    uint32_t width, uint32_t height)
{
    if (!m_initialized)
        return;

    auto& textureManager = render::TextureManager::Get();
    auto colorTexture = textureManager.CreateTexture(colorTextureInfo);
    auto depthTexture = textureManager.CreateTexture(depthTextureInfo);
    if (!colorTexture.IsValid() || !depthTexture.IsValid())
        return;

    m_engineViewportTexture = std::move(colorTexture);
    m_engineViewportDepthTexture = std::move(depthTexture);
    const Size requestedSize { width, height };
    m_engineViewportSize.store(requestedSize, std::memory_order_release);
    if (width > 0 && height > 0)
        m_commandQueue.Push(render::command::ResizeEngineViewport { width, height });
}

void RenderSystem::CreateEngineViewport(uint32_t width, uint32_t height)
{
    const Size requestedSize { width, height };
    const auto colorHandler = m_engineViewportTexture.GetHandler();
    const auto depthHandler = m_engineViewportDepthTexture.GetHandler();
    const auto currentSize = m_engineViewportSize.load(std::memory_order_acquire);
    if (!colorHandler.IsValid() || !depthHandler.IsValid() || width == 0 || height == 0 ||
        (m_resourceProvider->HasTexture(colorHandler) && m_resourceProvider->HasTexture(depthHandler) &&
            currentSize.width == requestedSize.width && currentSize.height == requestedSize.height))
        return;

    auto colorView = m_resourceProvider->CreateRenderTexture(colorHandler, width, height,
        render::ResourceViewType::RenderTarget);
    if (!colorView.IsValid())
        return;
    auto depthView = m_resourceProvider->CreateRenderTexture(depthHandler, width, height,
        render::ResourceViewType::DepthStencil);
    if (!depthView.IsValid()) {
        m_engineViewportRenderTarget = {};
        m_engineViewportDepthStencil = {};
        m_resourceProvider->ReleaseTexture(colorHandler);
        m_resourceProvider->ReleaseTexture(depthHandler);
        return;
    }

    m_engineViewportRenderTarget = std::move(colorView);
    m_engineViewportDepthStencil = std::move(depthView);
    m_engineViewportIsShaderResource = false;
    m_engineViewportSize.store(requestedSize, std::memory_order_release);
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
        system->m_commandQueue.Push(render::command::ResizeMainSwapChain {
            static_cast<uint32_t>(width), static_cast<uint32_t>(height)
        });
}

void RenderSystem::QueueEngineViewportResize(uint32_t width, uint32_t height)
{
    const auto currentSize = m_engineViewportSize.load(std::memory_order_acquire);
    if (width == 0 || height == 0 || (currentSize.width == width && currentSize.height == height))
        return;

    m_commandQueue.Push(render::command::ResizeEngineViewport { width, height });
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

void RenderSystem::CreateRenderSurface(const render::command::CreateRenderSurface& command)
{
    if (command.id == 0 || !m_resourceProvider || !command.nativeHandle ||
        command.width == 0 || command.height == 0)
        return;

    auto surface = m_resourceProvider->CreateSwapChain(command.width, command.height,
        command.nativeHandle, command.nativeDisplay, false);
    if (surface)
        m_renderSurfaces.insert_or_assign(command.id, std::move(surface));
}

void RenderSystem::ResizeRenderSurface(const render::command::ResizeRenderSurface& command)
{
    if (command.id == 0 || command.width == 0 || command.height == 0)
        return;
    if (const auto it = m_renderSurfaces.find(command.id); it != m_renderSurfaces.end())
        it->second.ResizeIfNeeded(command.width, command.height);
}

void RenderSystem::DestroyRenderSurface(const render::command::DestroyRenderSurface& command)
{
    if (command.id != 0)
        m_renderSurfaces.erase(command.id);
}

void RenderSystem::BeginRenderPass(const render::command::BeginRenderPass& command)
{
    if (!m_deviceContext || command.width == 0 || command.height == 0)
        return;

    render::SwapChain* surface = nullptr;
    if (command.surface == 0) {
        surface = &m_swapChain;
    } else if (const auto it = m_renderSurfaces.find(command.surface); it != m_renderSurfaces.end()) {
        surface = &it->second;
    }
    if (!surface)
        return;

    auto* renderTarget = surface->GetCurrentBackBufferRTV();
    if (!renderTarget)
        return;

    m_activeRenderSurface = command.surface;
    m_activeRenderSurfaceSize = Size { command.width, command.height };
    m_deviceContext->SetRenderTargets(1, &renderTarget, nullptr,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const Diligent::Viewport viewport {
        0.0f, 0.0f, static_cast<float>(command.width), static_cast<float>(command.height), 0.0f, 1.0f
    };
    m_deviceContext->SetViewports(1, &viewport, command.width, command.height);
    if (command.clear) {
        const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
        m_deviceContext->ClearRenderTarget(renderTarget, clearColor,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
}

void RenderSystem::DrawIndexed(const render::command::DrawIndexed& command)
{
    if (!m_deviceContext || !m_resourceProvider || !m_pOverlayPSO ||
        command.surface != m_activeRenderSurface ||
        command.vertices.empty() || command.indices.empty() ||
        command.indices.size() > (std::numeric_limits<Diligent::Uint32>::max)() ||
        command.scissor.right <= command.scissor.left ||
        command.scissor.bottom <= command.scissor.top)
        return;

    auto textureView = m_resourceProvider->GetShaderResourceView(command.texture);
    const auto resolvedTexture = textureView.Resolve();
    if (!resolvedTexture.view)
        return;

    const size_t vertexBytes = command.vertices.size() * sizeof(render::RenderVertex);
    const size_t indexBytes = command.indices.size() * sizeof(uint32_t);
    const auto vertexBuffer = m_bufferManager.CreateBuffer({
        "Overlay Vertex Buffer", render::BufferType::VertexBuffer, vertexBytes,
        const_cast<render::RenderVertex*>(command.vertices.data())
    });
    const auto indexBuffer = m_bufferManager.CreateBuffer({
        "Overlay Index Buffer", render::BufferType::IndexBuffer, indexBytes,
        const_cast<uint32_t*>(command.indices.data())
    });
    auto* vertexBufferImpl = m_bufferManager.GetBufferImpl(vertexBuffer);
    auto* indexBufferImpl = m_bufferManager.GetBufferImpl(indexBuffer);
    if (!vertexBufferImpl || !indexBufferImpl) {
        m_bufferManager.DestroyBuffer(vertexBuffer);
        m_bufferManager.DestroyBuffer(indexBuffer);
        return;
    }

    Diligent::IShaderResourceBinding* binding = nullptr;
    m_pOverlayPSO->CreateShaderResourceBinding(&binding, true);
    if (!binding) {
        m_bufferManager.DestroyBuffer(vertexBuffer);
        m_bufferManager.DestroyBuffer(indexBuffer);
        return;
    }
    auto* textureVariable = binding->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
    if (!textureVariable) {
        binding->Release();
        m_bufferManager.DestroyBuffer(vertexBuffer);
        m_bufferManager.DestroyBuffer(indexBuffer);
        return;
    }
    textureVariable->Set(resolvedTexture.view);

    m_deviceContext->SetPipelineState(m_pOverlayPSO);
    m_deviceContext->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const Diligent::Uint64 offset = 0;
    m_deviceContext->SetVertexBuffers(0, 1, &vertexBufferImpl, &offset,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    m_deviceContext->SetIndexBuffer(indexBufferImpl, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const Diligent::Rect scissor {
        static_cast<int32_t>(command.scissor.left),
        static_cast<int32_t>(command.scissor.top),
        static_cast<int32_t>(command.scissor.right),
        static_cast<int32_t>(command.scissor.bottom)
    };
    m_deviceContext->SetScissorRects(1, &scissor,
        m_activeRenderSurfaceSize.width, m_activeRenderSurfaceSize.height);
    const Diligent::DrawIndexedAttribs draw {
        static_cast<Diligent::Uint32>(command.indices.size()),
        Diligent::VT_UINT32,
        Diligent::DRAW_FLAG_VERIFY_ALL
    };
    m_deviceContext->DrawIndexed(draw);

    binding->Release();
    m_bufferManager.DestroyBuffer(vertexBuffer);
    m_bufferManager.DestroyBuffer(indexBuffer);
}

void RenderSystem::EndRenderPass(const render::command::EndRenderPass& command)
{
    if (command.surface != m_activeRenderSurface)
        return;
    if (command.present) {
        if (const auto it = m_renderSurfaces.find(command.surface); it != m_renderSurfaces.end())
            it->second.Present();
    }
    m_activeRenderSurface = 0;
    m_activeRenderSurfaceSize = {};
}

void RenderSystem::CommitCommands()
{
    m_commandQueue.CommitFrame();
}

void RenderSystem::ExecuteCommands(render::CommandList& commands)
{
    Executor executor(*this);
    commands.Execute(executor);
}

void RenderSystem::FlushCommands()
{
    if (!m_deviceContext)
        return;

    m_commandQueue.CommitFrame();
    m_commandQueue.BeginFrame();
    Executor executor(*this);
    m_commandQueue.Execute(executor);
}

void RenderSystem::BeginFrame()
{
    if (!m_swapChain || !m_deviceContext)
        return;

    m_commandQueue.BeginFrame();
    Executor executor(*this);
    m_commandQueue.Execute(executor);

    auto* pRTV = m_swapChain.GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain.GetDepthBufferDSV();

    const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
    m_deviceContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RenderSystem::Draw(const UnorderedMap<core::Handler, Vector<RenderObject>>& objects)
{
    if (objects.empty()) {
        return;
    }

    for (const auto& obj : objects) {
        if (obj.second.empty()) {
            continue;
        }

        const auto& mesh = m_meshManager.GetMesh(obj.first);
        if (mesh.indexCount == 0 || !mesh.vb.IsValid() || !mesh.ib.IsValid()) {
            continue;
        }

        Diligent::IBuffer* pVB = m_bufferManager.GetBufferImpl(mesh.vb);
        Diligent::IBuffer* pIB = m_bufferManager.GetBufferImpl(mesh.ib);
        if (!pVB || !pIB) {
            continue;
        }

        auto alloc = m_dynamicInstanceBuffer.Allocate(m_deviceContext, sizeof(RenderObject) * obj.second.size(), 16);
        std::memcpy(alloc.pCPUAddress, obj.second.data(), sizeof(RenderObject) * obj.second.size());

        const Diligent::Uint64 offsets[] = { 0, alloc.offset };
        Diligent::IBuffer* pVBs[] = { pVB, alloc.buffer };

        m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_deviceContext->SetIndexBuffer(pIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs { mesh.indexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
        DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(obj.second.size());
        m_deviceContext->DrawIndexed(DrawAttrs);
    }
}

void RenderSystem::Draw(FrameData& frameData)
{
    if (!m_deviceContext || !m_pPSO)
        return;

    const auto viewportColor = m_engineViewportRenderTarget.Resolve();
    const auto viewportDepth = m_engineViewportDepthStencil.Resolve();
    auto* viewportTexture = viewportColor.texture;
    auto* viewportRTV = viewportColor.view;
    auto* viewportDSV = viewportDepth.view;
    bool viewportRendered = false;
    if (viewportTexture && viewportRTV && viewportDSV) {
        if (m_engineViewportIsShaderResource) {
            Diligent::StateTransitionDesc toRenderTarget {
                viewportTexture,
                Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                Diligent::RESOURCE_STATE_RENDER_TARGET
            };
            m_deviceContext->TransitionResourceStates(1, &toRenderTarget);
            m_engineViewportIsShaderResource = false;
        }
        viewportTexture->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

        const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
        m_deviceContext->SetRenderTargets(1, &viewportRTV, viewportDSV,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        m_deviceContext->ClearRenderTarget(viewportRTV, clearColor,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        m_deviceContext->ClearDepthStencil(viewportDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        viewportRendered = true;
    }

    // 3. Update Camera Constant Buffer
    {
        struct CameraCBData {
            Matrix4x4 ViewProj;
            Vector4 CameraPos;
        };
        auto cameraAlloc = m_dynamicUniformBuffer.Allocate(m_deviceContext, sizeof(CameraCBData), 256);
        CameraCBData cbData;
        cbData.ViewProj = frameData.camera.GetViewProjectionMatrix();
        cbData.CameraPos = Vector4 { frameData.camera.GetPosition(), 1.0f };
        std::memcpy(cameraAlloc.pCPUAddress, &cbData, sizeof(CameraCBData));

        // Привязываем смещение кадра для переменной CameraConstants в SRB
        if (m_pSRB) {
            auto* pVar = m_pSRB->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
            if (pVar) {
                pVar->SetBufferOffset(cameraAlloc.offset);
            }
        }
    }

    // 5. Устанавливаем Pipeline State и финализируем SRB
    m_deviceContext->SetPipelineState(m_pPSO);
    m_deviceContext->CommitShaderResources(m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Draw(frameData.objects);

    // 6. Flush аллокаторов в конце кадра
    m_dynamicInstanceBuffer.Flush(m_deviceContext);
    m_dynamicUniformBuffer.Flush(m_deviceContext);
    if (viewportRendered) {
        Diligent::StateTransitionDesc toShaderResource {
            viewportTexture,
            Diligent::RESOURCE_STATE_RENDER_TARGET,
            Diligent::RESOURCE_STATE_SHADER_RESOURCE
        };
        m_deviceContext->TransitionResourceStates(1, &toShaderResource);
        viewportTexture->SetState(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
        m_engineViewportIsShaderResource = true;
    }
}

void RenderSystem::EndFrame()
{
    if (!m_swapChain || !m_deviceContext)
        return;
    ELM_PROFILE_SCOPE_N("Present");
    Executor executor(*this);
    m_commandQueue.Execute(executor);
    m_swapChain.Present();
}

void RenderSystem::Shutdown()
{
    if (!m_initialized)
        return;

    m_engineViewportRenderTarget = {};
    m_engineViewportDepthStencil = {};
    m_engineViewportTexture = {};
    m_engineViewportDepthTexture = {};

    // Render thread is stopped at this point: execute what is still queued (e.g. texture releases)
    FlushCommands();

    m_renderSurfaces.clear();
    m_resourceProvider->ClearTextures();
    m_resourceProvider.reset();

    m_bufferManager.Clear();

    m_dynamicInstanceBuffer.Release();
    m_dynamicUniformBuffer.Release();

    if (m_pSRB) {
        m_pSRB->Release();
        m_pSRB = nullptr;
    }
    if (m_pHighlightPSO) {
        m_pHighlightPSO->Release();
        m_pHighlightPSO = nullptr;
    }
    if (m_pOverlayPSO) {
        m_pOverlayPSO->Release();
        m_pOverlayPSO = nullptr;
    }
    if (m_pPSO) {
        m_pPSO->Release();
        m_pPSO = nullptr;
    }

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
