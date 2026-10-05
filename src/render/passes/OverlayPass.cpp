#include "render/passes/OverlayPass.hpp"

#include "core/Log.hpp"

#include "render/frame/FrameSnapshot.hpp"
#include "render/rhi/IRenderBackend.hpp"

#include <algorithm>
#include <cstdio>

namespace elm::render {

namespace {

    constexpr uint32_t TextureSlot = 0;

    void recordViewport(const OverlayViewport& viewport, TextureHandle fallbackTexture, PipelineHandle pipeline,
        rhi::ColorAttachment target, PassContext& context)
    {
        auto& commands = context.Commands();
        auto& uploads = context.Uploads();

        commands.BeginRenderPass(target);
        commands.SetPipeline(pipeline);

        const auto width = static_cast<float>(viewport.framebufferWidth);
        const auto height = static_cast<float>(viewport.framebufferHeight);
        for (const auto& list : viewport.drawLists) {
            if (list.vertices.empty() || list.indices.empty())
                continue;

            const auto vertices = uploads.Upload(rhi::UploadArena::Vertex, list.vertices.data(), list.vertices.size());
            const auto indices = uploads.Upload(rhi::UploadArena::Index, list.indices.data(), list.indices.size());
            if (!vertices.IsValid() || !indices.IsValid()) {
                ERROR_MESSAGE(log::Category::Render, "OverlayPass", "Upload arena is full, UI geometry skipped");
                break;
            }
            commands.BindVertexBuffer(0, rhi::BufferSource::FromUpload(vertices));
            commands.BindIndexBuffer(rhi::BufferSource::FromUpload(indices));

            for (const auto& command : list.commands) {
                if (command.hasUserCallback || command.elementCount == 0 ||
                    static_cast<size_t>(command.indexOffset) + command.elementCount > list.indices.size() ||
                    command.vertexOffset >= list.vertices.size())
                    continue;

                const auto left = static_cast<int32_t>(std::clamp(command.clipRect[0], 0.0f, width));
                const auto top = static_cast<int32_t>(std::clamp(command.clipRect[1], 0.0f, height));
                const auto right = static_cast<int32_t>(std::clamp(command.clipRect[2], 0.0f, width));
                const auto bottom = static_cast<int32_t>(std::clamp(command.clipRect[3], 0.0f, height));
                if (right <= left || bottom <= top)
                    continue;

                commands.SetScissor(left, top, right, bottom);
                commands.BindTexture(TextureSlot, command.texture.IsValid() ? command.texture : fallbackTexture, fallbackTexture);
                commands.DrawIndexed(command.elementCount, 1, command.indexOffset, command.vertexOffset);
            }
        }

        commands.EndRenderPass();
    }

} // namespace

bool OverlayPass::Init(rhi::IRenderBackend& backend)
{
    ShaderDesc pixelShader { "Overlay PS", ShaderStage::Pixel, "overlay.frag.hlsl", {} };
    // An sRGB backbuffer would convert again, so the shader blends in sRGB space itself
    if (isSrgbFormat(backend.GetSurfaceColorFormat()))
        pixelShader.macros.push_back({ "OVERLAY_MANUAL_SRGB", "1" });

    const auto vs = backend.CreateShader({ "Overlay VS", ShaderStage::Vertex, "overlay.vert.hlsl", {} });
    const auto ps = backend.CreateShader(pixelShader);
    if (!vs || !ps)
        return false;

    PipelineDesc desc;
    desc.name = "Overlay";
    desc.vertexShader = vs;
    desc.pixelShader = ps;
    desc.vertexLayout = {
        { 0, 0, VertexFormat::Float2, false }, // position (NDC)
        { 1, 0, VertexFormat::Float2, false }, // uv
        { 2, 0, VertexFormat::Float4, false }, // color
    };
    desc.colorFormat = backend.GetSurfaceColorFormat();
    desc.blend = BlendMode::PremultipliedAlpha;
    desc.scissor = true;
    desc.bindings = { { "g_Texture", ShaderStage::Pixel, BindingType::Texture, 0 } };
    desc.samplers = { { "g_Texture", ShaderStage::Pixel } };
    m_pipeline = backend.CreatePipeline(desc);
    return m_pipeline.IsValid();
}

void OverlayPass::Release(rhi::IRenderBackend& backend)
{
    backend.DestroyPipeline(m_pipeline);
    m_pipeline = {};
}

void OverlayPass::Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
    FrameBlackboard& blackboard)
{
    if (!m_pipeline)
        return;

    const auto fallbackTexture = frame.overlay.fallbackTexture;
    for (const auto& viewport : frame.overlay.viewports) {
        const SurfaceId surface = viewport.isMain ? MainSurface : viewport.id;
        if (!backend.HasSurface(surface) || viewport.framebufferWidth == 0 || viewport.framebufferHeight == 0)
            continue;

        char name[48];
        std::snprintf(name, sizeof(name), viewport.isMain ? "Overlay Main" : "Overlay %08x", viewport.id);
        const auto target = graph.ImportSurface(name, surface, { viewport.framebufferWidth, viewport.framebufferHeight });

        struct Data {
            const OverlayViewport* viewport { nullptr };
            RGTexture target;
        };
        graph.AddPass<Data>(name,
            [&](PassBuilder& builder, Data& data) {
                data.viewport = &viewport;
                data.target = builder.WriteColor(target);
                // The UI shows the scene view: its texture must be rendered and readable first
                if (blackboard.sceneColor.IsValid())
                    builder.Read(blackboard.sceneColor);
            },
            [pipeline = m_pipeline, fallbackTexture](const Data& data, PassContext& context) {
                constexpr float clearColor[4] = { 0.11f, 0.13f, 0.16f, 1.0f };
                recordViewport(*data.viewport, fallbackTexture, pipeline, context.ColorTarget(data.target, true, clearColor), context);
            });
    }
}

} // namespace elm::render
