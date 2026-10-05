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

    constexpr TextureFormat SceneColorFormat = TextureFormat::RGBA8_UNORM_SRGB;
    constexpr TextureFormat SceneDepthFormat = TextureFormat::D32_FLOAT;
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

    /// Draw items arrive sorted by mesh: every run of equal meshes becomes instanced draws.
    Vector<DrawRange> buildDrawRanges(const Vector<DrawItem>& drawItems)
    {
        Vector<DrawRange> ranges;
        for (size_t begin = 0; begin < drawItems.size();) {
            const MeshHandle mesh = drawItems[begin].mesh;
            size_t end = begin + 1;
            while (end < drawItems.size() && drawItems[end].mesh == mesh && end - begin < MaxInstancesPerDraw)
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
    desc.colorFormat = SceneColorFormat;
    desc.depthFormat = SceneDepthFormat;
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
    if (!m_pipeline || !frame.sceneView.IsValid())
        return;

    // The update side resizes the target through the command queue: draw at the size it actually has
    TextureDesc colorDesc;
    if (!backend.GetTextureDesc(frame.sceneView.colorTarget, colorDesc) || colorDesc.format != SceneColorFormat)
        return;
    const auto sceneColor = graph.ImportTexture("SceneColor", frame.sceneView.colorTarget, colorDesc);

    TextureDesc depthDesc;
    depthDesc.width = colorDesc.width;
    depthDesc.height = colorDesc.height;
    depthDesc.format = SceneDepthFormat;
    depthDesc.bindFlags = TextureBind::DepthStencil;

    struct Data {
        RGTexture color;
        RGTexture depth;
    };
    graph.AddPass<Data>("Scene",
        [&](PassBuilder& builder, Data& data) {
            data.color = builder.WriteColor(sceneColor);
            data.depth = builder.WriteDepth(builder.CreateTexture("SceneDepth", depthDesc));
        },
        [pipeline = m_pipeline](const Data& data, PassContext& context) {
            constexpr float clearColor[4] = { 0.11f, 0.13f, 0.16f, 1.0f };
            const auto& frame = context.Frame();
            auto& commands = context.Commands();
            auto& uploads = context.Uploads();

            const auto color = context.ColorTarget(data.color, false, clearColor);
            const auto depth = context.DepthTarget(data.depth, false);
            const CameraConstants constants { frame.sceneView.camera.viewProjection, Vector4 { frame.sceneView.camera.position, 1.0f } };
            const auto cameraConstants = uploads.Upload(rhi::UploadArena::Constants, &constants, 1);
            const auto ranges = buildDrawRanges(frame.drawItems);

            const auto beginScene = [&](rhi::CommandList& list) {
                list.BeginRenderPass(color, depth);
                list.SetPipeline(pipeline);
                list.SetConstants(CameraConstantsSlot, cameraConstants);
            };

            commands.BeginRenderPass(context.ColorTarget(data.color, true, clearColor), context.DepthTarget(data.depth, true));
            if (frame.drawItems.size() < ParallelInstanceThreshold) {
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

    blackboard.sceneColor = sceneColor;
}

} // namespace elm::render
