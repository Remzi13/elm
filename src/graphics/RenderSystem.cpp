#include "graphics/RenderSystem.hpp"

#include <GLFW/glfw3.h>

// Diligent Engine Includes
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#include "Graphics/GraphicsTools/interface/CommonlyUsedStates.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

// ImGui integration
#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"

#include <iostream>
#include <vector>
#include <iomanip>
#include <cstring>

#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

namespace Engine {

// HLSL Shader Source for 3D Mesh Rendering
static const char* VSSource = R"(
cbuffer CameraConstants
{
    row_major float4x4 g_ViewProj;
    float4   g_CameraPos;
};

struct VSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;

    float4 Row0   : ATTRIB3;
    float4 Row1   : ATTRIB4;
    float4 Row2   : ATTRIB5;
    float4 Row3   : ATTRIB6;
    float4 Color  : ATTRIB7;
};

struct PSInput
{
    float4 Pos      : SV_Position;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float3 WorldPos : WORLDPOS;
};

void main(in VSInput VSIn, out PSInput PSIn)
{
    float4x4 InstanceMat = MatrixFromRows(VSIn.Row0, VSIn.Row1, VSIn.Row2, VSIn.Row3);
    float4 WorldPos = mul(InstanceMat, float4(VSIn.Pos, 1.0));
    PSIn.WorldPos = WorldPos.xyz;
    PSIn.Pos = mul(g_ViewProj, WorldPos);

    float3x3 RotMat = (float3x3)InstanceMat;
    PSIn.Normal = normalize(mul(RotMat, VSIn.Normal));
    PSIn.Color = VSIn.Color;
}
)";

static const char* PSSource = R"(
struct PSInput
{
    float4 Pos      : SV_Position;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float3 WorldPos : WORLDPOS;
};

float4 main(in PSInput PSIn) : SV_Target
{
    float3 lightDir = normalize(float3(0.4, 0.9, -0.5));
    float diff = max(dot(PSIn.Normal, lightDir), 0.0);
    float3 ambient = float3(0.24, 0.26, 0.30);
    float3 diffuse = float3(0.85, 0.85, 0.82) * diff;
    float3 lighting = ambient + diffuse;
    return float4(PSIn.Color.rgb * lighting, PSIn.Color.a);
}
)";

static const char* PSHighlightSource = R"(
struct PSInput
{
    float4 Pos      : SV_Position;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float3 WorldPos : WORLDPOS;
};

float4 main(in PSInput PSIn) : SV_Target
{
    return float4(0.95, 0.20, 0.15, 0.40);
}
)";

RenderSystem::RenderSystem() = default;

RenderSystem::~RenderSystem() {
    Shutdown();
}

auto RenderSystem::Init(uint32_t width, uint32_t height, std::string_view title) -> EngineResult<void> {
    if (m_initialized) {
        return {};
    }

    m_width = width;
    m_height = height;

    if (!glfwInit()) {
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to initialize GLFW"));
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.data(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to create GLFW window"));
    }

    auto* pFactoryVk = Diligent::GetEngineFactoryVk();
    if (!pFactoryVk) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryVk"));
    }

    Diligent::EngineVkCreateInfo engineCreateInfo;
    engineCreateInfo.NumDeferredContexts = 0;
    engineCreateInfo.DynamicHeapSize = 32 << 20;
    pFactoryVk->CreateDeviceAndContextsVk(engineCreateInfo, &m_renderDevice, &m_deviceContext);

    if (!m_renderDevice || !m_deviceContext) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent Render Device & Contexts"));
    }

    Diligent::SwapChainDesc swapChainDesc;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;

    Diligent::LinuxNativeWindow nativeWindow;
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        nativeWindow.pDisplay = glfwGetWaylandDisplay();
        nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(glfwGetWaylandWindow(m_window)));
    } else {
        nativeWindow.pDisplay = glfwGetX11Display();
        nativeWindow.WindowId = static_cast<uint32_t>(glfwGetX11Window(m_window));
    }

    pFactoryVk->CreateSwapChainVk(m_renderDevice, m_deviceContext, swapChainDesc, nativeWindow, &m_swapChain);

    if (!m_swapChain) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent SwapChain"));
    }

    // Initialize ImGui
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    m_imguiContextCreated = true;

    const auto& SCDesc = m_swapChain->GetDesc();
    Diligent::ImGuiDiligentCreateInfo ImGuiCI;
    ImGuiCI.pDevice = m_renderDevice;
    ImGuiCI.BackBufferFmt = SCDesc.ColorBufferFormat;
    ImGuiCI.DepthBufferFmt = SCDesc.DepthBufferFormat;
    
    m_imGui = std::make_unique<Diligent::ImGuiImplDiligent>(ImGuiCI);
    ImGui_ImplGlfw_InitForOther(m_window, true);

    // Initialize 3D Rendering Pipeline
    InitPipeline();

    // Create Initial Scene
    RebuildScene();

    m_camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));

    m_initialized = true;
    std::cout << "[RenderSystem] Diligent Engine, 3D Mesh Pipeline, and SOC Testbed initialized." << std::endl;
    return {};
}

void RenderSystem::InitPipeline() {
    CreateMeshBuffers();
    CreateInstanceBuffer();

    // Uniform Buffer for Camera
    Diligent::BufferDesc CBDesc;
    CBDesc.Name           = "Camera Constants CB";
    CBDesc.Size           = sizeof(Matrix4x4) + sizeof(Vector4);
    CBDesc.Usage          = Diligent::USAGE_DYNAMIC;
    CBDesc.BindFlags      = Diligent::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    m_renderDevice->CreateBuffer(CBDesc, nullptr, &m_pCameraConstantsBuffer);

    // Create Shaders
    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
        ShaderCI.Desc.Name       = "Mesh VS";
        ShaderCI.Source          = VSSource;
        m_renderDevice->CreateShader(ShaderCI, &pVS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name       = "Mesh PS";
        ShaderCI.Source          = PSSource;
        m_renderDevice->CreateShader(ShaderCI, &pPS);
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pHighlightPS;
    {
        ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
        ShaderCI.Desc.Name       = "Highlight PS";
        ShaderCI.Source          = PSHighlightSource;
        m_renderDevice->CreateShader(ShaderCI, &pHighlightPS);
    }

    // Input Layout
    Diligent::LayoutElement LayoutElems[] = {
        // Slot 0: Per-vertex
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{2, 0, 2, Diligent::VT_FLOAT32, false},

        // Slot 1: Per-instance (Transform Matrix + Color)
        Diligent::LayoutElement{3, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
        Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
    };

    // Main Opaque Pipeline State
    Diligent::GraphicsPipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name = "Opaque Mesh PSO";
    auto& Pipeline = PSOCI.GraphicsPipeline;
    Pipeline.NumRenderTargets             = 1;
    Pipeline.RTVFormats[0]                = m_swapChain->GetDesc().ColorBufferFormat;
    Pipeline.DSVFormat                    = m_swapChain->GetDesc().DepthBufferFormat;
    Pipeline.PrimitiveTopology            = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Pipeline.RasterizerDesc.CullMode      = Diligent::CULL_MODE_NONE;
    Pipeline.DepthStencilDesc.DepthEnable = true;
    Pipeline.DepthStencilDesc.DepthWriteEnable = true;

    Pipeline.InputLayout.LayoutElements = LayoutElems;
    Pipeline.InputLayout.NumElements    = _countof(LayoutElems);

    PSOCI.pVS = pVS;
    PSOCI.pPS = pPS;

    PSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    m_renderDevice->CreateGraphicsPipelineState(PSOCI, &m_pPSO);
    if (m_pPSO) {
        auto* pVar = m_pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
        if (pVar) pVar->Set(m_pCameraConstantsBuffer);
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

    // Create Depth Preview Texture
    CreateDepthPreviewTexture(m_depthPreviewWidth, m_depthPreviewHeight);
    CreateEngineViewport(m_engineViewportWidth, m_engineViewportHeight);
}

void RenderSystem::CreateMeshBuffers() {
    auto createBuffers = [this](const MeshData& mesh, Diligent::IBuffer** ppVB, Diligent::IBuffer** ppIB, uint32_t& indexCount) {
        Diligent::BufferDesc VBDesc;
        VBDesc.Name      = "Mesh VB";
        VBDesc.Usage     = Diligent::USAGE_IMMUTABLE;
        VBDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
        VBDesc.Size      = mesh.vertices.size() * sizeof(Vertex);
        Diligent::BufferData VBData;
        VBData.pData    = mesh.vertices.data();
        VBData.DataSize = VBDesc.Size;
        m_renderDevice->CreateBuffer(VBDesc, &VBData, ppVB);

        Diligent::BufferDesc IBDesc;
        IBDesc.Name      = "Mesh IB";
        IBDesc.Usage     = Diligent::USAGE_IMMUTABLE;
        IBDesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
        IBDesc.Size      = mesh.indices.size() * sizeof(uint32_t);
        Diligent::BufferData IBData;
        IBData.pData    = mesh.indices.data();
        IBData.DataSize = IBDesc.Size;
        m_renderDevice->CreateBuffer(IBDesc, &IBData, ppIB);

        indexCount = static_cast<uint32_t>(mesh.indices.size());
    };

    const MeshData cubeMesh = GeometryPrimitives::CreateCube(1.0f);
    createBuffers(cubeMesh, &m_pCubeVB, &m_pCubeIB, m_cubeIndexCount);

    const MeshData wallMesh = GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f);
    createBuffers(wallMesh, &m_pWallVB, &m_pWallIB, m_wallIndexCount);

    const MeshData groundMesh = GeometryPrimitives::CreateGroundPlane(160.0f, 160.0f);
    createBuffers(groundMesh, &m_pGroundVB, &m_pGroundIB, m_groundIndexCount);
}

void RenderSystem::CreateInstanceBuffer() {
    Diligent::BufferDesc InstBuffDesc;
    InstBuffDesc.Name           = "Mesh Instance Buffer";
    InstBuffDesc.Usage          = Diligent::USAGE_DYNAMIC;
    InstBuffDesc.BindFlags      = Diligent::BIND_VERTEX_BUFFER;
    InstBuffDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    InstBuffDesc.Size           = sizeof(GpuInstanceData) * MaxInstances;
    m_renderDevice->CreateBuffer(InstBuffDesc, nullptr, &m_pInstanceBuffer);
}

void RenderSystem::CreateDepthPreviewTexture(uint32_t width, uint32_t height) {
    if (m_pDepthPreviewSRV) {
        m_pDepthPreviewSRV->Release();
        m_pDepthPreviewSRV = nullptr;
    }
    if (m_pDepthPreviewTex) {
        m_pDepthPreviewTex->Release();
        m_pDepthPreviewTex = nullptr;
    }

    m_depthPreviewWidth = width;
    m_depthPreviewHeight = height;
    m_depthPreviewPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000);

    Diligent::TextureDesc TexDesc;
    TexDesc.Name      = "Software Depth Buffer Preview Texture";
    TexDesc.Type      = Diligent::RESOURCE_DIM_TEX_2D;
    TexDesc.Width     = width;
    TexDesc.Height    = height;
    TexDesc.Format    = Diligent::TEX_FORMAT_RGBA8_UNORM;
    TexDesc.Usage     = Diligent::USAGE_DEFAULT;
    TexDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

    Diligent::TextureSubResData Level0Data;
    Level0Data.pData  = m_depthPreviewPixels.data();
    Level0Data.Stride = width * sizeof(uint32_t);
    Diligent::TextureData InitData;
    InitData.pSubResources   = &Level0Data;
    InitData.NumSubresources = 1;

    m_renderDevice->CreateTexture(TexDesc, &InitData, &m_pDepthPreviewTex);
    if (m_pDepthPreviewTex) {
        m_pDepthPreviewSRV = m_pDepthPreviewTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        if (m_pDepthPreviewSRV) {
            m_pDepthPreviewSRV->AddRef();
        }
    }
}

void RenderSystem::CreateEngineViewport(uint32_t width, uint32_t height) {
    if (m_pEngineViewportSRV) m_pEngineViewportSRV->Release();
    if (m_pEngineViewportDSV) m_pEngineViewportDSV->Release();
    if (m_pEngineViewportRTV) m_pEngineViewportRTV->Release();
    if (m_pEngineViewportTex) m_pEngineViewportTex->Release();
    m_pEngineViewportSRV = nullptr;
    m_pEngineViewportDSV = nullptr;
    m_pEngineViewportRTV = nullptr;
    m_pEngineViewportTex = nullptr;
    m_engineViewportIsShaderResource = false;

    m_engineViewportWidth = width;
    m_engineViewportHeight = height;

    Diligent::TextureDesc colorDesc;
    colorDesc.Name = "Engine Viewport Color";
    colorDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    colorDesc.Width = width;
    colorDesc.Height = height;
    colorDesc.Format = m_swapChain->GetDesc().ColorBufferFormat;
    colorDesc.Usage = Diligent::USAGE_DEFAULT;
    colorDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    m_renderDevice->CreateTexture(colorDesc, nullptr, &m_pEngineViewportTex);
    if (!m_pEngineViewportTex) return;
    m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

    m_pEngineViewportRTV = m_pEngineViewportTex->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
    m_pEngineViewportSRV = m_pEngineViewportTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
    if (m_pEngineViewportRTV) m_pEngineViewportRTV->AddRef();
    if (m_pEngineViewportSRV) m_pEngineViewportSRV->AddRef();

    Diligent::TextureDesc depthDesc;
    depthDesc.Name = "Engine Viewport Depth";
    depthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.Format = m_swapChain->GetDesc().DepthBufferFormat;
    depthDesc.Usage = Diligent::USAGE_DEFAULT;
    depthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
    Diligent::RefCntAutoPtr<Diligent::ITexture> depthTexture;
    m_renderDevice->CreateTexture(depthDesc, nullptr, &depthTexture);
    if (depthTexture) {
        depthTexture->SetState(Diligent::RESOURCE_STATE_DEPTH_WRITE);
        m_pEngineViewportDSV = depthTexture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        if (m_pEngineViewportDSV) m_pEngineViewportDSV->AddRef();
    }
}

void RenderSystem::UpdateDepthPreviewTexture() {
    if (!m_pDepthPreviewTex || !m_deviceContext) return;

    m_cullingSystem.GetDepthBuffer().GenerateVisualTexture(m_depthPreviewPixels, m_depthPreviewFalseColor);

    Diligent::Box UpdateBox;
    UpdateBox.MinX = 0;
    UpdateBox.MaxX = m_depthPreviewWidth;
    UpdateBox.MinY = 0;
    UpdateBox.MaxY = m_depthPreviewHeight;

    Diligent::TextureSubResData SubresData;
    SubresData.Stride = m_depthPreviewWidth * sizeof(uint32_t);
    SubresData.pData  = m_depthPreviewPixels.data();

    m_deviceContext->UpdateTexture(m_pDepthPreviewTex, 0, 0, UpdateBox, SubresData,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RenderSystem::RebuildScene() {
    TestScenes::BuildScene(m_currentPreset, static_cast<uint32_t>(m_targetInstanceCount), m_occluders, m_occludees);
}

void RenderSystem::Update(float deltaTime) {
    // Camera mouse look only when ImGui doesn't capture mouse or when right mouse button is dragging
    const ImGuiIO& io = ImGui::GetIO();
    const bool allowInput = !io.WantCaptureKeyboard || (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    m_camera.Update(deltaTime, m_window, allowInput);
}

bool RenderSystem::ShouldClose() const {
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

void RenderSystem::PollEvents() {
    glfwPollEvents();
}

void RenderSystem::BeginFrame() {
    if (!m_swapChain || !m_deviceContext) return;

    auto* pRTV = m_swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain->GetDepthBufferDSV();

    const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
    m_deviceContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    ImGui_ImplGlfw_NewFrame();
    m_imGui->NewFrame(m_width, m_height, m_swapChain->GetDesc().PreTransform);
}

void RenderSystem::RenderScene() {
    if (!m_deviceContext || !m_pPSO) return;

    if (m_pEngineViewportRTV && m_pEngineViewportDSV) {
        if (m_engineViewportIsShaderResource) {
            Diligent::StateTransitionDesc toRenderTarget{
                m_pEngineViewportTex,
                Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                Diligent::RESOURCE_STATE_RENDER_TARGET
            };
            m_deviceContext->TransitionResourceStates(1, &toRenderTarget);
            m_engineViewportIsShaderResource = false;
        }
        m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

        const float clearColor[] = {0.11f, 0.13f, 0.16f, 1.0f};
        m_deviceContext->SetRenderTargets(1, &m_pEngineViewportRTV, m_pEngineViewportDSV,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        m_deviceContext->ClearRenderTarget(m_pEngineViewportRTV, clearColor,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        m_deviceContext->ClearDepthStencil(m_pEngineViewportDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
    }

    // 1. Run Software Occlusion Culling
    const Matrix4x4 cullingVP = m_camera.GetCullingViewProjection();
    m_cullingSystem.ExecuteCulling(m_occluders, m_occludees, cullingVP);

    // 2. Update Depth Buffer Texture for ImGui
    UpdateDepthPreviewTexture();

    // 3. Update Camera Constant Buffer
    {
        struct CameraCBData {
            Matrix4x4 ViewProj;
            Vector4   CameraPos;
        };
        Diligent::MapHelper<CameraCBData> CBData(m_deviceContext, m_pCameraConstantsBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        CBData->ViewProj = m_camera.GetViewProjectionMatrix();
        CBData->CameraPos = Vector4{m_camera.GetPosition(), 1.0f};
    }

    // 4. Collect Visible and Culled Instances
    m_visibleGpuInstances.clear();
    m_culledGpuInstances.clear();

    for (const auto& inst : m_occludees) {
        if (inst.isVisible) {
            m_visibleGpuInstances.push_back({inst.worldTransform, inst.color});
        } else {
            m_culledGpuInstances.push_back({inst.worldTransform, Vector4{0.95f, 0.2f, 0.15f, 0.35f}});
        }
    }

    // 5. Render Ground & Occluders
    m_deviceContext->SetPipelineState(m_pPSO);
    m_deviceContext->CommitShaderResources(m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Draw Ground
    {
        GpuInstanceData groundInst{Matrix4x4::Translation(Vector3{0.0f, 0.0f, 0.0f}), Vector4{0.22f, 0.24f, 0.27f, 1.0f}};
        {
            Diligent::MapHelper<GpuInstanceData> InstData(m_deviceContext, m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            InstData[0] = groundInst;
        }

        const Diligent::Uint64 offsets[] = {0, 0};
        Diligent::IBuffer* pVBs[] = {m_pGroundVB, m_pInstanceBuffer};
        m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_deviceContext->SetIndexBuffer(m_pGroundIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs{m_groundIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL};
        DrawAttrs.NumInstances = 1;
        m_deviceContext->DrawIndexed(DrawAttrs);
    }

    // Draw Occluders (Walls) as one instanced batch to avoid repeated dynamic-buffer maps.
    const size_t numOccluders = std::min(m_occluders.size(), MaxInstances);
    if (numOccluders > 0) {
        std::vector<GpuInstanceData> occluderGpuInstances;
        occluderGpuInstances.reserve(numOccluders);
        for (size_t i = 0; i < numOccluders; ++i) {
            occluderGpuInstances.push_back({m_occluders[i].worldTransform, Vector4{0.35f, 0.38f, 0.44f, 1.0f}});
        }
        {
            Diligent::MapHelper<GpuInstanceData> InstData(m_deviceContext, m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            std::memcpy(InstData, occluderGpuInstances.data(), numOccluders * sizeof(GpuInstanceData));
        }

        const Diligent::Uint64 offsets[] = {0, 0};
        Diligent::IBuffer* pVBs[] = {m_pWallVB, m_pInstanceBuffer};
        m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_deviceContext->SetIndexBuffer(m_pWallIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs{m_wallIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL};
        DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numOccluders);
        m_deviceContext->DrawIndexed(DrawAttrs);
    }

    // 6. Draw Occludees (Cubes)
    if (m_cullingSystem.visualMode != VisualMode::OccludersOnly && !m_visibleGpuInstances.empty()) {
        const size_t numDraw = std::min(m_visibleGpuInstances.size(), MaxInstances);
        {
            Diligent::MapHelper<GpuInstanceData> InstData(m_deviceContext, m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            memcpy(InstData, m_visibleGpuInstances.data(), numDraw * sizeof(GpuInstanceData));
        }

        const Diligent::Uint64 offsets[] = {0, 0};
        Diligent::IBuffer* pVBs[] = {m_pCubeVB, m_pInstanceBuffer};
        m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_deviceContext->SetIndexBuffer(m_pCubeIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs{m_cubeIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL};
        DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numDraw);
        m_deviceContext->DrawIndexed(DrawAttrs);
    }

    // 7. Highlight Culled Objects (Ghost visualization)
    if (m_cullingSystem.visualMode == VisualMode::HighlightCulled && !m_culledGpuInstances.empty()) {
        const size_t numCulled = std::min(m_culledGpuInstances.size(), MaxInstances);
        {
            Diligent::MapHelper<GpuInstanceData> InstData(m_deviceContext, m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
            memcpy(InstData, m_culledGpuInstances.data(), numCulled * sizeof(GpuInstanceData));
        }

        m_deviceContext->SetPipelineState(m_pHighlightPSO);
        m_deviceContext->CommitShaderResources(m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const Diligent::Uint64 offsets[] = {0, 0};
        Diligent::IBuffer* pVBs[] = {m_pCubeVB, m_pInstanceBuffer};
        m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_deviceContext->SetIndexBuffer(m_pCubeIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs{m_cubeIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL};
        DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numCulled);
        m_deviceContext->DrawIndexed(DrawAttrs);
    }

    if (m_pEngineViewportTex) {
        Diligent::StateTransitionDesc toShaderResource{
            m_pEngineViewportTex,
            Diligent::RESOURCE_STATE_RENDER_TARGET,
            Diligent::RESOURCE_STATE_SHADER_RESOURCE
        };
        m_deviceContext->TransitionResourceStates(1, &toShaderResource);
        m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
        m_engineViewportIsShaderResource = true;
    }
}

void RenderSystem::RenderUI(const FrameStats& stats) {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->Pos);
    ImGui::SetNextWindowSize(mainViewport->Size);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    constexpr ImGuiWindowFlags dockspaceWindowFlags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("DockSpaceHost", nullptr, dockspaceWindowFlags);
    ImGui::PopStyleVar(2);
    ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowDockID(ImGui::GetID("MainDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Viewport");
    if (m_pEngineViewportSRV) {
        const ImVec2 availableSize = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(m_engineViewportWidth) / static_cast<float>(m_engineViewportHeight);
        ImVec2 imageSize = availableSize;
        if (imageSize.x / aspect < imageSize.y) {
            imageSize.y = imageSize.x / aspect;
        } else {
            imageSize.x = imageSize.y * aspect;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(m_pEngineViewportSRV), imageSize);
    }
    ImGui::End();

    // -------------------------------------------------------------------------
    // Window 1: SOC Controls & Telemetry
    // -------------------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);

    ImGui::Begin("Software Occlusion Culling Lab");

    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
    ImGui::Text("FPS: %.1f | Frame: %.2f ms", stats.fps, stats.deltaTimeMs);
    ImGui::Separator();

    // Scene Selection
    if (ImGui::CollapsingHeader("Scene Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* presets[] = {"The Great Wall & City Grid", "Rooms & Corridors", "Physics Barrier Sandbox"};
        int currentPresetIdx = static_cast<int>(m_currentPreset);
        if (ImGui::Combo("Preset", &currentPresetIdx, presets, IM_ARRAYSIZE(presets))) {
            m_currentPreset = static_cast<ScenePreset>(currentPresetIdx);
            RebuildScene();
        }

        if (ImGui::SliderInt("Instances", &m_targetInstanceCount, 100, 10000)) {
            RebuildScene();
        }
    }

    // Culling Settings
    if (ImGui::CollapsingHeader("Culling Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Frustum Culling", &m_cullingSystem.enableFrustumCulling);
        ImGui::Checkbox("Enable Software Occlusion Culling", &m_cullingSystem.enableOcclusionCulling);

        bool freezeCamera = m_camera.IsCullingFrozen();
        if (ImGui::Checkbox("Freeze Culling Camera", &freezeCamera)) {
            m_camera.SetFreezeCulling(freezeCamera);
        }
        if (freezeCamera) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[FROZEN]");
        }

        ImGui::SliderFloat("Depth Bias", &m_cullingSystem.depthBias, 0.0f, 0.01f, "%.4f");

        // Resolution Selector
        const char* resolutions[] = {"64x36", "128x72", "256x144 (Recommended)", "320x180", "512x288"};
        static int currentResIdx = 2; // 256x144
        if (ImGui::Combo("SOC Buffer Res", &currentResIdx, resolutions, IM_ARRAYSIZE(resolutions))) {
            uint32_t w = 256, h = 144;
            if (currentResIdx == 0) { w = 64; h = 36; }
            else if (currentResIdx == 1) { w = 128; h = 72; }
            else if (currentResIdx == 2) { w = 256; h = 144; }
            else if (currentResIdx == 3) { w = 320; h = 180; }
            else if (currentResIdx == 4) { w = 512; h = 288; }
            m_cullingSystem.SetResolution(w, h);
            CreateDepthPreviewTexture(w, h);
        }
    }

    // Visual Modes
    if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int vMode = static_cast<int>(m_cullingSystem.visualMode);
        ImGui::RadioButton("Hide Culled (Draw Visible Only)", &vMode, 0);
        ImGui::RadioButton("Highlight Culled (Red Ghost)", &vMode, 1);
        ImGui::RadioButton("Occluders Only", &vMode, 2);
        m_cullingSystem.visualMode = static_cast<VisualMode>(vMode);
    }

    // Culling Telemetry & Metrics
    if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& cStats = m_cullingSystem.GetStats();

        ImGui::Text("Total Objects:     %u", cStats.totalObjects);
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", cStats.visibleCount);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", cStats.frustumCulledCount);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", cStats.occlusionCulledCount);

        ImGui::Spacing();
        ImGui::ProgressBar(cStats.cullingRatioPercent / 100.0f, ImVec2(-1, 0), "");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("Culled: %.1f%%", cStats.cullingRatioPercent);

        ImGui::Separator();
        ImGui::Text("CPU Rasterize Time:   %.1f us (%.3f ms)", cStats.rasterizeTimeUs, cStats.rasterizeTimeUs / 1000.0f);
        ImGui::Text("CPU Query / Test:     %.1f us (%.3f ms)", cStats.queryTimeUs, cStats.queryTimeUs / 1000.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time:   %.1f us (%.3f ms)", cStats.totalCullingTimeUs, cStats.totalCullingTimeUs / 1000.0f);
    }

    // Camera Info & Controls Help
    if (ImGui::CollapsingHeader("Camera & Controls")) {
        const Vector3 pos = m_camera.GetPosition();
        ImGui::Text("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        ImGui::SliderFloat("Move Speed", &m_camera.moveSpeed, 5.0f, 50.0f);
        ImGui::Separator();
        ImGui::TextDisabled("Controls:");
        ImGui::BulletText("Right Mouse Button + Drag: Look around");
        ImGui::BulletText("W / A / S / D: Move forward / left / back / right");
        ImGui::BulletText("E / Q: Move Up / Down");
        ImGui::BulletText("Left Shift: Sprint (2.5x speed)");
    }

    ImGui::End();

    // -------------------------------------------------------------------------
    // Window 2: Real-Time Software Depth Buffer Viewer
    // -------------------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(m_width) - 460.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);

    ImGui::Begin("Software Depth Buffer Viewport");

    ImGui::Text("Resolution: %ux%u", m_depthPreviewWidth, m_depthPreviewHeight);
    ImGui::SameLine();
    ImGui::Checkbox("False Color (Heatmap)", &m_depthPreviewFalseColor);

    if (m_pDepthPreviewSRV) {
        const float aspect = static_cast<float>(m_depthPreviewWidth) / static_cast<float>(m_depthPreviewHeight);
        const float displayWidth = ImGui::GetContentRegionAvail().x;
        const float displayHeight = displayWidth / aspect;

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(m_pDepthPreviewSRV), ImVec2(displayWidth, displayHeight));

        // Hover inspector
        if (ImGui::IsItemHovered()) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const float u = (mousePos.x - imagePos.x) / displayWidth;
            const float v = (mousePos.y - imagePos.y) / displayHeight;
            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                const uint32_t px = static_cast<uint32_t>(u * static_cast<float>(m_depthPreviewWidth));
                const uint32_t py = static_cast<uint32_t>(v * static_cast<float>(m_depthPreviewHeight));
                const float depthVal = m_cullingSystem.GetDepthBuffer().GetDepth(px, py);

                ImGui::BeginTooltip();
                ImGui::Text("Pixel: (%u, %u)", px, py);
                ImGui::Text("Normalized Depth: %.4f", depthVal);
                if (depthVal >= 0.999f) {
                    ImGui::TextDisabled("(Clear / Sky)");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "Occluder Geometry");
                }
                ImGui::EndTooltip();
            }
        }
    }

    ImGui::End();

}

void RenderSystem::EndFrame() {
    if (!m_swapChain || !m_deviceContext) return;

    auto* pRTV = m_swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain->GetDepthBufferDSV();
    m_deviceContext->SetRenderTargets(1, &pRTV, pDSV,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (m_imGui) {
        m_imGui->Render(m_deviceContext);
    }

    m_swapChain->Present();

    ImGui::UpdatePlatformWindows();
}

void RenderSystem::Shutdown() {
    if (!m_initialized) return;

    if (m_pDepthPreviewSRV) {
        m_pDepthPreviewSRV->Release();
        m_pDepthPreviewSRV = nullptr;
    }
    if (m_pDepthPreviewTex) {
        m_pDepthPreviewTex->Release();
        m_pDepthPreviewTex = nullptr;
    }

    if (m_pEngineViewportSRV) m_pEngineViewportSRV->Release();
    if (m_pEngineViewportDSV) m_pEngineViewportDSV->Release();
    if (m_pEngineViewportRTV) m_pEngineViewportRTV->Release();
    if (m_pEngineViewportTex) m_pEngineViewportTex->Release();
    m_pEngineViewportSRV = nullptr;
    m_pEngineViewportDSV = nullptr;
    m_pEngineViewportRTV = nullptr;
    m_pEngineViewportTex = nullptr;

    if (m_pInstanceBuffer) {
        m_pInstanceBuffer->Release();
        m_pInstanceBuffer = nullptr;
    }
    if (m_pGroundIB) {
        m_pGroundIB->Release();
        m_pGroundIB = nullptr;
    }
    if (m_pGroundVB) {
        m_pGroundVB->Release();
        m_pGroundVB = nullptr;
    }
    if (m_pWallIB) {
        m_pWallIB->Release();
        m_pWallIB = nullptr;
    }
    if (m_pWallVB) {
        m_pWallVB->Release();
        m_pWallVB = nullptr;
    }
    if (m_pCubeIB) {
        m_pCubeIB->Release();
        m_pCubeIB = nullptr;
    }
    if (m_pCubeVB) {
        m_pCubeVB->Release();
        m_pCubeVB = nullptr;
    }

    if (m_pCameraConstantsBuffer) {
        m_pCameraConstantsBuffer->Release();
        m_pCameraConstantsBuffer = nullptr;
    }
    if (m_pSRB) {
        m_pSRB->Release();
        m_pSRB = nullptr;
    }
    if (m_pHighlightPSO) {
        m_pHighlightPSO->Release();
        m_pHighlightPSO = nullptr;
    }
    if (m_pPSO) {
        m_pPSO->Release();
        m_pPSO = nullptr;
    }

    m_imGui.reset();

    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    if (m_deviceContext) {
        m_deviceContext->Release();
        m_deviceContext = nullptr;
    }
    if (m_renderDevice) {
        m_renderDevice->Release();
        m_renderDevice = nullptr;
    }

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();

    m_initialized = false;
    std::cout << "[RenderSystem] Shutdown completed." << std::endl;
}

} // namespace Engine
