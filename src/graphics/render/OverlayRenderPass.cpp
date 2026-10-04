#include "graphics/render/OverlayRenderPass.hpp"

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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

namespace elm::render {

namespace {

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

class OverlayFrameRenderer {
public:
    OverlayFrameRenderer(RenderFrameContext& frameContext, Diligent::IPipelineState* pipelineState,
        UnorderedMap<RenderSurfaceId, SwapChain>& viewportSurfaces)
        : m_resources(frameContext.resources)
        , m_pipelineState(pipelineState)
        , m_viewportSurfaces(viewportSurfaces)
    {
    }

    [[nodiscard]] uint64_t Render(const OverlayFrame& frame)
    {
        uint64_t releasedViewportCount = 0;
        for (const auto& event : frame.surfaceEvents) {
            switch (event.type) {
            case OverlaySurfaceEvent::Type::Create:
                CreateViewportSurface(event);
                break;
            case OverlaySurfaceEvent::Type::Destroy:
                m_viewportSurfaces.erase(event.id);
                ++releasedViewportCount;
                break;
            case OverlaySurfaceEvent::Type::Resize:
                ResizeViewportSurface(event);
                break;
            }
        }

        for (const auto& snapshot : frame.viewports)
            RenderViewport(snapshot, frame.fallbackTexture);
        return releasedViewportCount;
    }

private:
    void CreateViewportSurface(const OverlaySurfaceEvent& event)
    {
        if (event.id == 0 || !m_resources.resourceProvider || !event.nativeHandle ||
            event.width == 0 || event.height == 0)
            return;

        auto surface = m_resources.resourceProvider->CreateSwapChain(event.width, event.height,
            event.nativeHandle, event.nativeDisplay, false);
        if (surface)
            m_viewportSurfaces.insert_or_assign(event.id, std::move(surface));
    }

    void ResizeViewportSurface(const OverlaySurfaceEvent& event)
    {
        if (event.id == 0 || event.width == 0 || event.height == 0)
            return;
        if (const auto it = m_viewportSurfaces.find(event.id); it != m_viewportSurfaces.end())
            it->second.ResizeIfNeeded(event.width, event.height);
    }

    void RenderViewport(const OverlayViewport& snapshot, core::Handler fallbackTexture)
    {
        const auto surfaceId = snapshot.isMain ? 0 : snapshot.id;
        SwapChain* surface = nullptr;
        if (snapshot.isMain) {
            surface = m_resources.mainSwapChain;
        } else if (const auto it = m_viewportSurfaces.find(surfaceId); it != m_viewportSurfaces.end()) {
            surface = &it->second;
        }
        if (!surface || !m_resources.deviceContext ||
            snapshot.framebufferWidth == 0 || snapshot.framebufferHeight == 0)
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

        for (const auto& list : snapshot.drawLists) {
            for (const auto& drawCommand : list.commands)
                DrawCommand(snapshot, list, drawCommand, fallbackTexture);
        }

        if (!snapshot.isMain)
            surface->Present();
    }

    void DrawCommand(const OverlayViewport& snapshot, const OverlayDrawList& list,
        const OverlayDrawCommand& drawCommand, core::Handler fallbackTexture)
    {
        if (drawCommand.hasUserCallback || drawCommand.elementCount == 0 ||
            !m_resources.deviceContext || !m_resources.resourceProvider || !m_resources.bufferManager ||
            !m_pipelineState || drawCommand.elementCount > (std::numeric_limits<Diligent::Uint32>::max)())
            return;

        const auto left = static_cast<uint32_t>(std::clamp(drawCommand.clipRect[0], 0.0f, static_cast<float>(snapshot.framebufferWidth)));
        const auto top = static_cast<uint32_t>(std::clamp(drawCommand.clipRect[1], 0.0f, static_cast<float>(snapshot.framebufferHeight)));
        const auto right = static_cast<uint32_t>(std::clamp(drawCommand.clipRect[2], 0.0f, static_cast<float>(snapshot.framebufferWidth)));
        const auto bottom = static_cast<uint32_t>(std::clamp(drawCommand.clipRect[3], 0.0f, static_cast<float>(snapshot.framebufferHeight)));
        if (right <= left || bottom <= top)
            return;

        auto textureView = m_resources.resourceProvider->GetShaderResourceView(
            drawCommand.texture.IsValid() ? drawCommand.texture : fallbackTexture);
        const auto resolvedTexture = textureView.Resolve();
        if (!resolvedTexture.view)
            return;

        Vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(drawCommand.elementCount));
        for (uint32_t index = 0; index < drawCommand.elementCount; ++index) {
            const auto sourceIndex = drawCommand.indexOffset + index;
            if (sourceIndex >= list.indices.size())
                return;
            const auto vertexIndex = static_cast<uint64_t>(list.indices[sourceIndex]) + drawCommand.vertexOffset;
            if (vertexIndex >= list.vertices.size())
                return;
            indices.push_back(static_cast<uint32_t>(vertexIndex));
        }

        const BufferInfo vertexBufferInfo {
            "Overlay Vertex Buffer", BufferType::VertexBuffer,
            list.vertices.size() * sizeof(OverlayVertex), list.vertices.data()
        };
        const BufferInfo indexBufferInfo {
            "Overlay Index Buffer", BufferType::IndexBuffer,
            indices.size() * sizeof(uint32_t), indices.data()
        };
        const auto vertexBuffer = m_resources.bufferManager->CreateBuffer(vertexBufferInfo);
        const auto indexBuffer = m_resources.bufferManager->CreateBuffer(indexBufferInfo);
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
    OverlayFrameRenderer renderer(context, m_pOverlayPSO, m_viewportSurfaces);
    context.overlay.releasedSurfaceCount += renderer.Render(context.overlay);
}

} // namespace elm::render
