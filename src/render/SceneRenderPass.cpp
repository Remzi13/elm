#include "render/SceneRenderPass.hpp"

#include "render/RenderSystem.hpp"
#include "render/BufferManager.hpp"
#include "render/DynamicLinearAllocator.hpp"
#include "render/MeshManager.h"
#include "render/RenderResourceProvider.hpp"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace elm::render {

namespace {

String LoadSceneShaderSource(const char* fileName)
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

} // namespace

SceneRenderPass::~SceneRenderPass()
{
    ReleaseResources();
}

void SceneRenderPass::Initialize(Diligent::IRenderDevice* device,
    Diligent::IBuffer* dynamicUniformBuffer,
    Diligent::TEXTURE_FORMAT colorFormat,
    Diligent::TEXTURE_FORMAT depthFormat)
{
    if (!device || !dynamicUniformBuffer)
        return;

    Diligent::ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

    const String vsSource = LoadSceneShaderSource("mesh.vert.hlsl");
    const String psSource = LoadSceneShaderSource("mesh.frag.hlsl");
    const String highlightPsSource = LoadSceneShaderSource("highlight.frag.hlsl");
    if (vsSource.empty() || psSource.empty() || highlightPsSource.empty()) {
        std::cerr << "[SceneRenderPass] Pipeline creation aborted: shader source is missing." << std::endl;
        return;
    }

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderCI.Desc.Name = "Mesh VS";
    shaderCI.Source = vsSource.c_str();
    device->CreateShader(shaderCI, &pVS);

    Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderCI.Desc.Name = "Mesh PS";
    shaderCI.Source = psSource.c_str();
    device->CreateShader(shaderCI, &pPS);

    Diligent::RefCntAutoPtr<Diligent::IShader> pHighlightPS;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderCI.Desc.Name = "Highlight PS";
    shaderCI.Source = highlightPsSource.c_str();
    device->CreateShader(shaderCI, &pHighlightPS);

    if (!pVS || !pPS || !pHighlightPS) {
        std::cerr << "[SceneRenderPass] Pipeline creation aborted: shader compilation failed." << std::endl;
        return;
    }

    Diligent::LayoutElement layoutElems[] = {
        Diligent::LayoutElement { 0, 0, 3, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 1, 0, 3, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 2, 0, 2, Diligent::VT_FLOAT32, false },
        Diligent::LayoutElement { 3, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 4, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 5, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 6, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE },
        Diligent::LayoutElement { 7, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE }
    };

    Diligent::GraphicsPipelineStateCreateInfo psci;
    psci.PSODesc.Name = "Opaque Mesh PSO";
    auto& pipeline = psci.GraphicsPipeline;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = colorFormat;
    pipeline.DSVFormat = depthFormat;
    pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    pipeline.DepthStencilDesc.DepthEnable = true;
    pipeline.DepthStencilDesc.DepthWriteEnable = true;
    pipeline.InputLayout.LayoutElements = layoutElems;
    pipeline.InputLayout.NumElements = _countof(layoutElems);
    psci.pVS = pVS;
    psci.pPS = pPS;
    psci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    Diligent::IPipelineState* opaquePSO = nullptr;
    device->CreateGraphicsPipelineState(psci, &opaquePSO);
    Diligent::IShaderResourceBinding* srb = nullptr;
    if (opaquePSO) {
        auto* pVar = opaquePSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
        if (pVar)
            pVar->Set(dynamicUniformBuffer);
        opaquePSO->CreateShaderResourceBinding(&srb, true);
    }

    Diligent::GraphicsPipelineStateCreateInfo highlightPSCI = psci;
    Diligent::IPipelineState* highlightPSO = nullptr;
    highlightPSCI.PSODesc.Name = "Highlight Culled PSO";
    highlightPSCI.pPS = pHighlightPS;
    highlightPSCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    auto& blend0 = highlightPSCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend0.BlendEnable = true;
    blend0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend0.DestBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
    device->CreateGraphicsPipelineState(highlightPSCI, &highlightPSO);

    SetPipelineState(opaquePSO, highlightPSO, srb);
}

void SceneRenderPass::SetPipelineState(Diligent::IPipelineState* opaquePSO,
    Diligent::IPipelineState* highlightPSO,
    Diligent::IShaderResourceBinding* srb) noexcept
{
    m_pOpaquePSO = opaquePSO;
    m_pHighlightPSO = highlightPSO;
    m_pSRB = srb;
}

void SceneRenderPass::EnsureViewportTarget(RenderPassContext& ctx, const ViewPort::Snapshot& viewPort)
{
    if (!ctx.resourceProvider || !viewPort.IsValid()) {
        m_viewportRenderTarget = {};
        m_viewportDepthStencil = {};
        m_viewportColorHandler = {};
        m_viewportDepthHandler = {};
        m_viewportTargetSize = {};
        m_viewportIsShaderResource = false;
        return;
    }

    if (m_viewportColorHandler == viewPort.colorTexture &&
        m_viewportDepthHandler == viewPort.depthTexture &&
        m_viewportRenderTarget.IsValid() && m_viewportDepthStencil.IsValid() &&
        m_viewportTargetSize.width == viewPort.size.width &&
        m_viewportTargetSize.height == viewPort.size.height)
        return;

    m_viewportRenderTarget = {};
    m_viewportDepthStencil = {};

    auto colorView = ctx.resourceProvider->CreateRenderTexture(
        viewPort.colorTexture, viewPort.size.width, viewPort.size.height,
        ResourceViewType::RenderTarget);
    if (!colorView.IsValid())
        return;
    auto depthView = ctx.resourceProvider->CreateRenderTexture(
        viewPort.depthTexture, viewPort.size.width, viewPort.size.height,
        ResourceViewType::DepthStencil);
    if (!depthView.IsValid()) {
        return;
    }

    m_viewportRenderTarget = std::move(colorView);
    m_viewportDepthStencil = std::move(depthView);
    m_viewportColorHandler = viewPort.colorTexture;
    m_viewportDepthHandler = viewPort.depthTexture;
    m_viewportTargetSize = viewPort.size;
    m_viewportIsShaderResource = false;
}

void SceneRenderPass::Execute(RenderFrameContext& frameContext)
{
    auto& ctx = frameContext.resources;
    auto& frameData = frameContext.frameData;
    if (!ctx.deviceContext || !m_pOpaquePSO)
        return;

    EnsureViewportTarget(ctx, frameContext.viewPort);

    const auto viewportColor = m_viewportRenderTarget.Resolve();
    const auto viewportDepth = m_viewportDepthStencil.Resolve();
    auto* viewportTexture = viewportColor.texture;
    auto* viewportRTV = viewportColor.view;
    auto* viewportDSV = viewportDepth.view;
    bool viewportRendered = false;
    if (viewportTexture && viewportRTV && viewportDSV) {
        if (m_viewportIsShaderResource) {
            Diligent::StateTransitionDesc toRenderTarget {
                viewportTexture,
                Diligent::RESOURCE_STATE_SHADER_RESOURCE,
                Diligent::RESOURCE_STATE_RENDER_TARGET
            };
            ctx.deviceContext->TransitionResourceStates(1, &toRenderTarget);
            m_viewportIsShaderResource = false;
        }
        viewportTexture->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

        const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
        ctx.deviceContext->SetRenderTargets(1, &viewportRTV, viewportDSV,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        ctx.deviceContext->ClearRenderTarget(viewportRTV, clearColor,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        ctx.deviceContext->ClearDepthStencil(viewportDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
        viewportRendered = true;
    }

    // Update Camera Constant Buffer
    {
        struct CameraCBData {
            Matrix4x4 ViewProj;
            Vector4 CameraPos;
        };
        auto cameraAlloc = ctx.dynamicUniformBuffer->Allocate(ctx.deviceContext, sizeof(CameraCBData), 256);
        CameraCBData cbData;
        cbData.ViewProj = frameData.camera.GetViewProjectionMatrix();
        cbData.CameraPos = Vector4 { frameData.camera.GetPosition(), 1.0f };
        std::memcpy(cameraAlloc.pCPUAddress, &cbData, sizeof(CameraCBData));

        if (m_pSRB) {
            auto* pVar = m_pSRB->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
            if (pVar) {
                pVar->SetBufferOffset(cameraAlloc.offset);
            }
        }
    }

    ctx.deviceContext->SetPipelineState(m_pOpaquePSO);
    ctx.deviceContext->CommitShaderResources(m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawObjects(ctx, frameData.objects);

    ctx.dynamicInstanceBuffer->Flush(ctx.deviceContext);
    ctx.dynamicUniformBuffer->Flush(ctx.deviceContext);
    if (viewportRendered) {
        Diligent::StateTransitionDesc toShaderResource {
            viewportTexture,
            Diligent::RESOURCE_STATE_RENDER_TARGET,
            Diligent::RESOURCE_STATE_SHADER_RESOURCE
        };
        ctx.deviceContext->TransitionResourceStates(1, &toShaderResource);
        viewportTexture->SetState(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
        m_viewportIsShaderResource = true;
    }
}

void SceneRenderPass::DrawObjects(RenderPassContext& ctx,
    const UnorderedMap<core::Handler, Vector<RenderObject>>& objects)
{
    if (objects.empty())
        return;

    for (const auto& obj : objects) {
        if (obj.second.empty())
            continue;

        const auto& mesh = ctx.meshManager->GetMesh(obj.first);
        if (mesh.indexCount == 0 || !mesh.vb.IsValid() || !mesh.ib.IsValid())
            continue;

        Diligent::IBuffer* pVB = ctx.bufferManager->GetBufferImpl(mesh.vb);
        Diligent::IBuffer* pIB = ctx.bufferManager->GetBufferImpl(mesh.ib);
        if (!pVB || !pIB)
            continue;

        auto alloc = ctx.dynamicInstanceBuffer->Allocate(ctx.deviceContext,
            sizeof(RenderObject) * obj.second.size(), 16);
        std::memcpy(alloc.pCPUAddress, obj.second.data(), sizeof(RenderObject) * obj.second.size());

        const Diligent::Uint64 offsets[] = { 0, alloc.offset };
        Diligent::IBuffer* pVBs[] = { pVB, alloc.buffer };

        ctx.deviceContext->SetVertexBuffers(0, 2, pVBs, offsets,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        ctx.deviceContext->SetIndexBuffer(pIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::DrawIndexedAttribs DrawAttrs { mesh.indexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
        DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(obj.second.size());
        ctx.deviceContext->DrawIndexed(DrawAttrs);
    }
}

void SceneRenderPass::ReleasePipelineState()
{
    if (m_pSRB) {
        m_pSRB->Release();
        m_pSRB = nullptr;
    }
    if (m_pHighlightPSO) {
        m_pHighlightPSO->Release();
        m_pHighlightPSO = nullptr;
    }
    if (m_pOpaquePSO) {
        m_pOpaquePSO->Release();
        m_pOpaquePSO = nullptr;
    }
}

void SceneRenderPass::ReleaseResources()
{
    ReleasePipelineState();
    m_viewportRenderTarget = {};
    m_viewportDepthStencil = {};
    m_viewportColorHandler = {};
    m_viewportDepthHandler = {};
    m_viewportTargetSize = {};
}

} // namespace elm::render
