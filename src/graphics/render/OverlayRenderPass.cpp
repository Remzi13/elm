#include "graphics/render/OverlayRenderPass.hpp"

#include "graphics/ImGuiSystem.hpp"
#include "graphics/render/BufferManager.hpp"
#include "graphics/render/RenderResourceProvider.hpp"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceVariable.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

namespace elm::render {

namespace {

struct OverlayVertex {
    float position[2];
    float uv[2];
    float color[4];
};

String LoadOverlayShaderSource(const char* fileName)
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
        if (file.read(source.data(), size))
            return source;
    }
    return { };
}

core::Handler GetTextureHandler(ImTextureID textureId, core::Handler fallback)
{
    const auto value = reinterpret_cast<uintptr_t>(textureId);
    if ((value & 1) == 0)
        return fallback;
    return core::Handler(static_cast<core::Handler::ValueType>(value >> 1), core::Handler::Render);
}

OverlayVertex ConvertVertex(const ImDrawVert& vertex, const ImVec2& displayPosition, const ImVec2& displaySize)
{
    const auto color = static_cast<uint32_t>(vertex.col);
    constexpr float colorScale = 1.0f / 255.0f;
    return {
        {
            ((vertex.pos.x - displayPosition.x) / displaySize.x) * 2.0f - 1.0f,
            1.0f - ((vertex.pos.y - displayPosition.y) / displaySize.y) * 2.0f
        },
        { vertex.uv.x, vertex.uv.y },
        {
            static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xff) * colorScale,
            static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xff) * colorScale,
            static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xff) * colorScale,
            static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xff) * colorScale
        }
    };
}

class OverlayFrameRenderer {
public:
    OverlayFrameRenderer(RenderFrameContext& frameContext, Diligent::IPipelineState* pipelineState,
        UnorderedMap<RenderSurfaceId, SwapChain>& viewportSurfaces)
        : m_resources(frameContext.resources)
        , m_pipelineState(pipelineState)
        , m_viewportSurfaces(viewportSurfaces)
    {
    }

    [[nodiscard]] uint64_t Render(const ImGuiFrame& frame, core::Handler fallbackTexture)
    {
        uint64_t releasedViewportCount = 0;
        for (const auto& event : frame.viewportEvents) {
            switch (event.type) {
            case ImGuiViewportEvent::Type::Create:
                CreateViewportSurface(event);
                break;
            case ImGuiViewportEvent::Type::Destroy:
                m_viewportSurfaces.erase(event.id);
                ++releasedViewportCount;
                break;
            case ImGuiViewportEvent::Type::Resize:
                ResizeViewportSurface(event);
                break;
            }
        }

        for (const auto& snapshot : frame.viewports)
            RenderViewport(snapshot, fallbackTexture);
        return releasedViewportCount;
    }

private:
    void CreateViewportSurface(const ImGuiViewportEvent& event)
    {
        if (event.id == 0 || !m_resources.resourceProvider || !event.nativeHandle ||
            event.width == 0 || event.height == 0)
            return;

        auto surface = m_resources.resourceProvider->CreateSwapChain(event.width, event.height,
            event.nativeHandle, event.nativeDisplay, false);
        if (surface)
            m_viewportSurfaces.insert_or_assign(event.id, std::move(surface));
    }

    void ResizeViewportSurface(const ImGuiViewportEvent& event)
    {
        if (event.id == 0 || event.width == 0 || event.height == 0)
            return;
        if (const auto it = m_viewportSurfaces.find(event.id); it != m_viewportSurfaces.end())
            it->second.ResizeIfNeeded(event.width, event.height);
    }

    void RenderViewport(const ImGuiViewportSnapshot& snapshot, core::Handler fallbackTexture)
    {
        const auto surfaceId = snapshot.isMain ? 0 : snapshot.id;
        SwapChain* surface = nullptr;
        if (snapshot.isMain) {
            surface = m_resources.mainSwapChain;
        } else if (const auto it = m_viewportSurfaces.find(surfaceId); it != m_viewportSurfaces.end()) {
            surface = &it->second;
        }
        if (!surface || !m_resources.deviceContext ||
            snapshot.framebufferWidth == 0 || snapshot.framebufferHeight == 0 ||
            snapshot.drawData.DisplaySize.x <= 0.0f || snapshot.drawData.DisplaySize.y <= 0.0f)
            return;

        auto* renderTarget = surface->GetCurrentBackBufferRTV();
        if (!renderTarget)
            return;

        m_resources.deviceContext->SetRenderTargets(1, &renderTarget, nullptr,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport {
            0.0f, 0.0f, static_cast<float>(snapshot.framebufferWidth),
            static_cast<float>(snapshot.framebufferHeight), 0.0f, 1.0f
        };
        m_resources.deviceContext->SetViewports(1, &viewport,
            snapshot.framebufferWidth, snapshot.framebufferHeight);
        if (!snapshot.isMain) {
            const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
            m_resources.deviceContext->ClearRenderTarget(renderTarget, clearColor,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        for (const ImDrawList* list : snapshot.drawLists) {
            for (const ImDrawCmd& drawCommand : list->CmdBuffer)
                DrawCommand(snapshot, *list, drawCommand, fallbackTexture);
        }

        if (!snapshot.isMain)
            surface->Present();
    }

    void DrawCommand(const ImGuiViewportSnapshot& snapshot, const ImDrawList& list,
        const ImDrawCmd& drawCommand, core::Handler fallbackTexture)
    {
        if (drawCommand.UserCallback || drawCommand.ElemCount == 0 ||
            !m_resources.deviceContext || !m_resources.resourceProvider || !m_resources.bufferManager ||
            !m_pipelineState || drawCommand.ElemCount > (std::numeric_limits<Diligent::Uint32>::max)())
            return;

        const auto& drawData = snapshot.drawData;
        const float clipLeft = (drawCommand.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
        const float clipTop = (drawCommand.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
        const float clipRight = (drawCommand.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x;
        const float clipBottom = (drawCommand.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y;
        const auto left = static_cast<uint32_t>(std::clamp(clipLeft, 0.0f, static_cast<float>(snapshot.framebufferWidth)));
        const auto top = static_cast<uint32_t>(std::clamp(clipTop, 0.0f, static_cast<float>(snapshot.framebufferHeight)));
        const auto right = static_cast<uint32_t>(std::clamp(clipRight, 0.0f, static_cast<float>(snapshot.framebufferWidth)));
        const auto bottom = static_cast<uint32_t>(std::clamp(clipBottom, 0.0f, static_cast<float>(snapshot.framebufferHeight)));
        if (right <= left || bottom <= top)
            return;

        auto textureView = m_resources.resourceProvider->GetShaderResourceView(
            GetTextureHandler(drawCommand.TextureId, fallbackTexture));
        const auto resolvedTexture = textureView.Resolve();
        if (!resolvedTexture.view)
            return;

        Vector<OverlayVertex> vertices;
        vertices.reserve(static_cast<size_t>(list.VtxBuffer.Size));
        for (const ImDrawVert& vertex : list.VtxBuffer)
            vertices.push_back(ConvertVertex(vertex, drawData.DisplayPos, drawData.DisplaySize));

        Vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(drawCommand.ElemCount));
        for (uint32_t index = 0; index < drawCommand.ElemCount; ++index) {
            const auto sourceIndex = drawCommand.IdxOffset + index;
            if (sourceIndex >= static_cast<uint32_t>(list.IdxBuffer.Size))
                return;
            indices.push_back(static_cast<uint32_t>(list.IdxBuffer[sourceIndex]) + drawCommand.VtxOffset);
        }

        const auto vertexBuffer = m_resources.bufferManager->CreateBuffer({
            "Overlay Vertex Buffer", BufferType::VertexBuffer,
            vertices.size() * sizeof(OverlayVertex), vertices.data()
        });
        const auto indexBuffer = m_resources.bufferManager->CreateBuffer({
            "Overlay Index Buffer", BufferType::IndexBuffer,
            indices.size() * sizeof(uint32_t), indices.data()
        });
        auto* vertexBufferImpl = m_resources.bufferManager->GetBufferImpl(vertexBuffer);
        auto* indexBufferImpl = m_resources.bufferManager->GetBufferImpl(indexBuffer);
        if (!vertexBufferImpl || !indexBufferImpl) {
            m_resources.bufferManager->DestroyBuffer(vertexBuffer);
            m_resources.bufferManager->DestroyBuffer(indexBuffer);
            return;
        }

        Diligent::IShaderResourceBinding* binding = nullptr;
        m_pipelineState->CreateShaderResourceBinding(&binding, true);
        if (!binding) {
            m_resources.bufferManager->DestroyBuffer(vertexBuffer);
            m_resources.bufferManager->DestroyBuffer(indexBuffer);
            return;
        }

        auto* textureVariable = binding->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
        if (!textureVariable) {
            binding->Release();
            m_resources.bufferManager->DestroyBuffer(vertexBuffer);
            m_resources.bufferManager->DestroyBuffer(indexBuffer);
            return;
        }
        textureVariable->Set(resolvedTexture.view);

        m_resources.deviceContext->SetPipelineState(m_pipelineState);
        m_resources.deviceContext->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Uint64 offset = 0;
        m_resources.deviceContext->SetVertexBuffers(0, 1, &vertexBufferImpl, &offset,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_resources.deviceContext->SetIndexBuffer(indexBufferImpl, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Rect scissor {
            static_cast<int32_t>(left), static_cast<int32_t>(top),
            static_cast<int32_t>(right), static_cast<int32_t>(bottom)
        };
        m_resources.deviceContext->SetScissorRects(1, &scissor,
            snapshot.framebufferWidth, snapshot.framebufferHeight);
        const Diligent::DrawIndexedAttribs draw {
            static_cast<Diligent::Uint32>(indices.size()),
            Diligent::VT_UINT32,
            Diligent::DRAW_FLAG_VERIFY_ALL
        };
        m_resources.deviceContext->DrawIndexed(draw);

        binding->Release();
        m_resources.bufferManager->DestroyBuffer(vertexBuffer);
        m_resources.bufferManager->DestroyBuffer(indexBuffer);
    }

    RenderPassContext& m_resources;
    Diligent::IPipelineState* m_pipelineState { nullptr };
    UnorderedMap<RenderSurfaceId, SwapChain>& m_viewportSurfaces;
};

} // namespace

OverlayRenderPass::~OverlayRenderPass()
{
    ReleaseResources();
}

void OverlayRenderPass::Initialize(Diligent::IRenderDevice* device, Diligent::TEXTURE_FORMAT colorFormat)
{
    if (!device)
        return;

    const String vsSource = LoadOverlayShaderSource("overlay.vert.hlsl");
    const String psSource = LoadOverlayShaderSource("overlay.frag.hlsl");
    if (vsSource.empty() || psSource.empty()) {
        std::cerr << "[OverlayRenderPass] Pipeline creation aborted: shader source is missing." << std::endl;
        return;
    }

    Diligent::ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderCI.Desc.Name = "Overlay VS";
    shaderCI.Source = vsSource.c_str();
    device->CreateShader(shaderCI, &pVS);

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderCI.Desc.Name = "Overlay PS";
    shaderCI.Source = psSource.c_str();
    const Diligent::ShaderMacro overlayGammaMacro[] {
        { "OVERLAY_MANUAL_SRGB", "1" }
    };
    if (device->GetTextureFormatInfo(colorFormat).ComponentType == Diligent::COMPONENT_TYPE_UNORM_SRGB)
        shaderCI.Macros = { overlayGammaMacro, _countof(overlayGammaMacro) };
    else
        shaderCI.Macros = {};
    device->CreateShader(shaderCI, &pPS);
    if (!pVS || !pPS)
        return;

    Diligent::LayoutElement layout[] = {
        Diligent::LayoutElement { 0, 0, 2, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 1, 0, 2, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 2, 0, 4, Diligent::VT_FLOAT32, false }
    };

    Diligent::GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "Overlay PSO";
    auto& pipeline = psoCI.GraphicsPipeline;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = colorFormat;
    pipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
    pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    pipeline.RasterizerDesc.ScissorEnable = true;
    pipeline.DepthStencilDesc.DepthEnable = false;
    pipeline.InputLayout.LayoutElements = layout;
    pipeline.InputLayout.NumElements = _countof(layout);
    auto& blend0 = pipeline.BlendDesc.RenderTargets[0];
    blend0.BlendEnable = true;
    blend0.SrcBlend = Diligent::BLEND_FACTOR_ONE;
    blend0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend0.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    psoCI.pVS = pVS;
    psoCI.pPS = pPS;

    Diligent::ShaderResourceVariableDesc variable {
        Diligent::SHADER_TYPE_PIXEL,
        "g_Texture",
        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE
    };
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    psoCI.PSODesc.ResourceLayout.Variables = &variable;
    psoCI.PSODesc.ResourceLayout.NumVariables = 1;
    Diligent::ImmutableSamplerDesc sampler;
    sampler.ShaderStages = Diligent::SHADER_TYPE_PIXEL;
    sampler.SamplerOrTextureName = "g_Texture_sampler";
    sampler.Desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.Desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.Desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    sampler.Desc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.Desc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    sampler.Desc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = &sampler;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    device->CreateGraphicsPipelineState(psoCI, &m_pOverlayPSO);
}

void OverlayRenderPass::ReleaseResources()
{
    ReleaseViewportSurfaces();
    if (m_pOverlayPSO) {
        m_pOverlayPSO->Release();
        m_pOverlayPSO = nullptr;
    }
}

void OverlayRenderPass::ReleaseViewportSurfaces()
{
    m_viewportSurfaces.clear();
}

void OverlayRenderPass::Execute(RenderFrameContext& context)
{
    if (!context.uiFrame)
        return;

    OverlayFrameRenderer renderer(context, m_pOverlayPSO, m_viewportSurfaces);
    context.uiFrame->releasedViewportCount += renderer.Render(*context.uiFrame, context.fallbackTexture);
}

} // namespace elm::render
