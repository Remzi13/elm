#include "render/passes/ScenePass.hpp"

#include "core/Log.hpp"

#include "render/frame/FrameSnapshot.hpp"
#include "render/rhi/IRenderBackend.hpp"

namespace elm::render {

namespace {

    struct CameraConstants {
        Matrix4x4 viewProjection;
        Vector4 cameraPosition;
    };

    static_assert(sizeof(InstanceData) == sizeof(float) * 20, "Instance layout must match the per-instance vertex stream");

    constexpr uint32_t CameraConstantsSlot = 0;

    /// Instances per draw call and draws per command list when the scene is recorded in batches.
    constexpr size_t MaxInstancesPerDraw = 1024;
    constexpr size_t DrawsPerBatch = 4;
    /// Below this many instances one command list is cheaper than spreading the work.
    constexpr size_t ParallelInstanceThreshold = 4096;

    /// A contiguous range of draw items sharing one mesh.
    struct DrawRange {
        MeshHandle mesh;
        size_t begin { 0 };
        size_t end { 0 };
    };

    /// Draw items of a view arrive sorted by mesh: every run of equal meshes becomes instanced draws.
    Vector<DrawRange> buildDrawRanges(const Vector<DrawItem>& drawItems, size_t first, size_t last)
    {
        Vector<DrawRange> ranges;
        for (size_t begin = first; begin < last;) {
            const MeshHandle mesh = drawItems[begin].mesh;
            size_t end = begin + 1;
            while (end < last && drawItems[end].mesh == mesh && end - begin < MaxInstancesPerDraw)
                ++end;
            ranges.push_back({ mesh, begin, end });
            begin = end;
        }
        return ranges;
    }

    void recordDraws(const Vector<DrawItem>& drawItems, const DrawRange* ranges, size_t rangeCount,
        rhi::CommandList& commands, rhi::UploadAllocator& uploads)
    {
        for (size_t r = 0; r < rangeCount; ++r) {
            const auto& range = ranges[r];
            const auto count = static_cast<uint32_t>(range.end - range.begin);

            auto instances = uploads.Allocate(rhi::UploadArena::Vertex, sizeof(InstanceData) * count);
            if (!instances.IsValid()) {
                ERROR_MESSAGE(log::Category::Render, "ScenePass", "Instance upload arena is full, %u instances skipped", count);
                continue;
            }
            auto* data = static_cast<InstanceData*>(instances.data);
            for (size_t i = range.begin; i < range.end; ++i)
                *data++ = drawItems[i].instance;

            commands.BindVertexBuffer(0, rhi::BufferSource::Vertices(range.mesh));
            commands.BindVertexBuffer(1, rhi::BufferSource::FromUpload(instances.ref));
            commands.BindIndexBuffer(rhi::BufferSource::Indices(range.mesh));
            commands.DrawIndexed(0, count);
        }
    }

} // namespace

bool ScenePass::Init(rhi::IRenderBackend& backend)
{
    const auto vs = backend.CreateShader({ "Mesh VS", ShaderStage::Vertex, "mesh.vert.hlsl", {} });
    const auto ps = backend.CreateShader({ "Mesh PS", ShaderStage::Pixel, "mesh.frag.hlsl", {} });
    if (!vs || !ps)
        return false;

    PipelineDesc desc;
    desc.name = "Opaque Mesh";
    desc.vertexShader = vs;
    desc.pixelShader = ps;
    desc.vertexLayout = {
        { 0, 0, VertexFormat::Float3, false }, // position
        { 1, 0, VertexFormat::Float3, false }, // normal
        { 2, 0, VertexFormat::Float2, false }, // uv
        { 3, 1, VertexFormat::Float4, true }, // instance transform rows
        { 4, 1, VertexFormat::Float4, true },
        { 5, 1, VertexFormat::Float4, true },
        { 6, 1, VertexFormat::Float4, true },
        { 7, 1, VertexFormat::Float4, true }, // instance color
    };
    desc.colorFormat = SceneViewTargets::ColorFormat;
    desc.depthFormat = SceneViewTargets::DepthFormat;
    desc.depthTest = true;
    desc.depthWrite = true;
    desc.bindings = { { "CameraConstants", ShaderStage::Vertex, BindingType::Constants, sizeof(CameraConstants) } };
    m_pipeline = backend.CreatePipeline(desc);
    return m_pipeline.IsValid();
}

void ScenePass::Release(rhi::IRenderBackend& backend)
{
    backend.DestroyPipeline(m_pipeline);
    m_pipeline = {};
}

void ScenePass::Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
    FrameBlackboard& blackboard)
{
    if (!m_pipeline)
        return;

    for (size_t viewIndex = 0; viewIndex < frame.sceneViews.size(); ++viewIndex) {
        const auto& view = frame.sceneViews[viewIndex];
        if (!view.IsValid())
            continue;

        // The update side resizes the target through the command queue: draw at the size it actually has
        TextureDesc colorDesc;
        if (!backend.GetTextureDesc(view.colorTarget, colorDesc) || colorDesc.format != SceneViewTargets::ColorFormat)
            continue;
        const String suffix = viewIndex == 0 ? String() : " " + String(std::to_string(viewIndex));
        const auto sceneColor = graph.ImportTexture("SceneColor" + suffix, view.colorTarget, colorDesc);

        TextureDesc depthDesc;
        depthDesc.width = colorDesc.width;
        depthDesc.height = colorDesc.height;
        depthDesc.format = SceneViewTargets::DepthFormat;
        depthDesc.bindFlags = TextureBind::DepthStencil;

        struct Data {
            RGTexture color;
            RGTexture depth;
        };
        const Data& scene = graph.AddPass<Data>("Scene" + suffix,
            [&](PassBuilder& builder, Data& data) {
                data.color = builder.WriteColor(sceneColor);
                data.depth = builder.WriteDepth(builder.CreateTexture("SceneDepth" + suffix, depthDesc));
            },
            [pipeline = m_pipeline, viewIndex](const Data& data, PassContext& context) {
                constexpr float clearColor[4] = { 0.11f, 0.13f, 0.16f, 1.0f };
                const auto& frame = context.Frame();
                const auto& view = frame.sceneViews[viewIndex];
                auto& commands = context.Commands();
                auto& uploads = context.Uploads();

                const auto color = context.ColorTarget(data.color, false, clearColor);
                const auto depth = context.DepthTarget(data.depth, false);
                const CameraConstants constants { view.camera.viewProjection, Vector4 { view.camera.position, 1.0f } };
                const auto cameraConstants = uploads.Upload(rhi::UploadArena::Constants, &constants, 1);
                const auto ranges = buildDrawRanges(frame.drawItems, view.firstDraw, view.firstDraw + view.drawCount);

                const auto beginScene = [&](rhi::CommandList& list) {
                    list.BeginRenderPass(color, depth);
                    list.SetPipeline(pipeline);
                    list.SetConstants(CameraConstantsSlot, cameraConstants);
                };

                commands.BeginRenderPass(context.ColorTarget(data.color, true, clearColor), context.DepthTarget(data.depth, true));
                if (view.drawCount < ParallelInstanceThreshold) {
                    commands.SetPipeline(pipeline);
                    commands.SetConstants(CameraConstantsSlot, cameraConstants);
                    recordDraws(frame.drawItems, ranges.data(), ranges.size(), commands, uploads);
                    commands.EndRenderPass();
                    return;
                }
                commands.EndRenderPass();

                // Large scenes: batches of draws recorded on the job system, submitted after the clear
                context.RecordParallel(ranges.size(), DrawsPerBatch, [&](rhi::CommandList& list, size_t begin, size_t end) {
                    beginScene(list);
                    recordDraws(frame.drawItems, ranges.data() + begin, end - begin, list, uploads);
                    list.EndRenderPass();
                });
            });

        blackboard.sceneViews.push_back({ viewIndex, sceneColor, scene.depth });
    }
}

} // namespace elm::render
