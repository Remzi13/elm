#include "render/passes/DebugDrawPass.hpp"

#include "core/Log.hpp"

#include "render/frame/FrameSnapshot.hpp"
#include "render/rhi/IRenderBackend.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace elm::render {

namespace {

    struct CameraConstants {
        Matrix4x4 viewProjection;
    };

    struct PassTargets {
        RGTexture color;
        RGTexture depth;
    };

    using StylePipelines = std::array<PipelineHandle, AllDebugStyles.size()>;

    constexpr uint32_t CameraConstantsSlot = 0;
    /// Tested primitives first, so the on-top ones end up over them.
    constexpr std::array DrawOrder { DebugDepth::Tested, DebugDepth::OnTop };

    PipelineHandle createPipeline(rhi::IRenderBackend& backend, ShaderHandle vs, ShaderHandle ps, DebugStyle style)
    {
        const bool lines = style.topology == DebugTopology::Lines;
        const bool tested = style.depth == DebugDepth::Tested;

        PipelineDesc desc;
        desc.name = String("Debug ") + (lines ? "Lines" : "Triangles") + (tested ? " Tested" : " On Top");
        desc.vertexShader = vs;
        desc.pixelShader = ps;
        desc.vertexLayout = {
            { 0, 0, VertexFormat::Float3, false }, // position
            { 1, 0, VertexFormat::UNorm8x4, false }, // color
        };
        desc.colorFormat = SceneViewTargets::ColorFormat;
        desc.depthFormat = SceneViewTargets::DepthFormat;
        desc.topology = lines ? PrimitiveTopology::LineList : PrimitiveTopology::TriangleList;
        desc.blend = BlendMode::AlphaBlend;
        desc.depthTest = tested;
        desc.depthWrite = false;
        desc.bindings = { { "CameraConstants", ShaderStage::Vertex, BindingType::Constants, sizeof(CameraConstants) } };
        return backend.CreatePipeline(desc);
    }

    /// batches share one vertex upload that starts at vertex firstVertex of the frame.
    void recordBatches(std::span<const DebugBatch> batches, uint32_t firstVertex, rhi::UploadRef vertices,
        rhi::UploadRef camera, const StylePipelines& pipelines, rhi::CommandList& commands)
    {
        for (const DebugDepth depth : DrawOrder) {
            for (const DebugBatch& batch : batches) {
                if (batch.style.depth != depth || batch.vertexCount == 0)
                    continue;
                commands.SetPipeline(pipelines[DebugStyleIndex(batch.style)]);
                commands.SetConstants(CameraConstantsSlot, camera);
                commands.BindVertexBuffer(0, rhi::BufferSource::FromUpload(vertices));
                commands.Draw(batch.vertexCount, 1, batch.firstVertex - firstVertex);
            }
        }
    }

    void recordView(const PassTargets& targets, size_t viewIndex, const StylePipelines& pipelines, PassContext& context)
    {
        const auto& frame = context.Frame();
        const auto& view = frame.sceneViews[viewIndex];
        const std::span batches { frame.debug.batches.data() + view.firstDebugBatch, view.debugBatchCount };
        const uint32_t firstVertex = batches.front().firstVertex;
        const uint32_t vertexCount = batches.back().firstVertex + batches.back().vertexCount - firstVertex;

        auto& uploads = context.Uploads();
        const CameraConstants constants { view.camera.viewProjection };
        const auto camera = uploads.Upload(rhi::UploadArena::Constants, &constants, 1);
        const auto vertices = uploads.Upload(rhi::UploadArena::Vertex, frame.debug.vertices.data() + firstVertex, vertexCount);
        if (!camera.IsValid() || !vertices.IsValid()) {
            ERROR_MESSAGE(log::Category::Render, "DebugDrawPass", "Upload arena is full, %u debug vertices skipped", vertexCount);
            return;
        }

        constexpr float unusedClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        auto& commands = context.Commands();
        commands.BeginRenderPass(context.ColorTarget(targets.color, false, unusedClearColor), context.DepthTarget(targets.depth, false));
        recordBatches(batches, firstVertex, vertices, camera, pipelines, commands);
        commands.EndRenderPass();
    }

} // namespace

bool DebugDrawPass::Init(rhi::IRenderBackend& backend)
{
    const auto vs = backend.CreateShader({ "Debug VS", ShaderStage::Vertex, "debug.vert.hlsl", {} });
    const auto ps = backend.CreateShader({ "Debug PS", ShaderStage::Pixel, "debug.frag.hlsl", {} });
    if (!vs || !ps)
        return false;

    bool created = true;
    for (const DebugStyle style : AllDebugStyles) {
        PipelineHandle& pipeline = m_pipelines[DebugStyleIndex(style)];
        pipeline = createPipeline(backend, vs, ps, style);
        created = created && pipeline.IsValid();
    }
    return created;
}

void DebugDrawPass::Release(rhi::IRenderBackend& backend)
{
    for (auto& pipeline : m_pipelines) {
        backend.DestroyPipeline(pipeline);
        pipeline = {};
    }
}

void DebugDrawPass::Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend&,
    FrameBlackboard& blackboard)
{
    if (!std::ranges::all_of(m_pipelines, [](PipelineHandle pipeline) { return pipeline.IsValid(); }))
        return;

    for (const SceneViewTargets& view : blackboard.sceneViews) {
        if (frame.sceneViews[view.viewIndex].debugBatchCount == 0)
            continue;

        const String suffix = view.viewIndex == 0 ? String() : " " + String(std::to_string(view.viewIndex));
        graph.AddPass<PassTargets>("Debug Draw" + suffix,
            [&](PassBuilder& builder, PassTargets& targets) {
                // Writing the targets of ScenePass places this pass after it and before the UI reads them
                targets.color = builder.WriteColor(view.color);
                targets.depth = builder.WriteDepth(view.depth);
            },
            [pipelines = m_pipelines, viewIndex = view.viewIndex](const PassTargets& targets, PassContext& context) {
                recordView(targets, viewIndex, pipelines, context);
            });
    }
}

} // namespace elm::render
