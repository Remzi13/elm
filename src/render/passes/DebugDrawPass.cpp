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
                { 0, 0, VertexFormat::Float3, false },   // position
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

        /// Debug primitives of one view as they are uploaded; batches index into vertices.
        struct ViewGeometry {
            Vector<DebugVertex> vertices;
            Vector<DebugBatch> batches;
        };

        /// Part [enter, exit] of the clip-space segment a -> b inside the view volume (D3D depth 0..w).
        /// False when the segment is entirely outside.
        bool clipSegment(const Vector4& a, const Vector4& b, float& enter, float& exit)
        {
            // Signed distances to the six planes, inside when >= 0
            const auto distances = [](const Vector4& p) {
                return std::array { p.w + p.x, p.w - p.x, p.w + p.y, p.w - p.y, p.z, p.w - p.z };
            };
            const auto fromA = distances(a);
            const auto fromB = distances(b);
            enter = 0.0f;
            exit = 1.0f;
            for (size_t plane = 0; plane < fromA.size(); ++plane) {
                if (fromA[plane] < 0.0f && fromB[plane] < 0.0f)
                    return false;
                const float crossing = fromA[plane] / (fromA[plane] - fromB[plane]);
                if (fromA[plane] < 0.0f)
                    enter = (std::max)(enter, crossing);
                else if (fromB[plane] < 0.0f)
                    exit = (std::min)(exit, crossing);
            }
            return enter <= exit;
        }

        /// Lines are clipped to the view volume on the CPU. Some rasterizers (llvmpipe) fill garbage
        /// when a line is clipped by the GPU far outside the screen, e.g. a long collider edge that
        /// passes beside and behind the camera. Triangles are copied as they are.
        void appendBatch(const DebugBatch& batch, const Vector<DebugVertex>& source, const Matrix4x4& viewProjection, ViewGeometry& out)
        {
            DebugBatch clipped { batch.style, static_cast<uint32_t>(out.vertices.size()), 0 };
            const auto first = source.begin() + batch.firstVertex;
            if (batch.style.topology == DebugTopology::Triangles) {
                out.vertices.insert(out.vertices.end(), first, first + batch.vertexCount);
            } else {
                for (uint32_t index = 0; index + 1 < batch.vertexCount; index += 2) {
                    DebugVertex a = first[index];
                    DebugVertex b = first[index + 1];
                    float enter = 0.0f;
                    float exit = 1.0f;
                    if (!clipSegment(viewProjection.TransformPoint(a.position), viewProjection.TransformPoint(b.position), enter, exit))
                        continue;
                    const Vector3 direction = b.position - a.position;
                    b.position = a.position + direction * exit;
                    a.position = a.position + direction * enter;
                    out.vertices.push_back(a);
                    out.vertices.push_back(b);
                }
            }
            clipped.vertexCount = static_cast<uint32_t>(out.vertices.size()) - clipped.firstVertex;
            if (clipped.vertexCount > 0)
                out.batches.push_back(clipped);
        }

        ViewGeometry buildViewGeometry(std::span<const DebugBatch> batches, const Vector<DebugVertex>& vertices, const Matrix4x4& viewProjection)
        {
            ViewGeometry geometry;
            geometry.batches.reserve(batches.size());
            for (const DebugBatch& batch : batches)
                appendBatch(batch, vertices, viewProjection, geometry);
            return geometry;
        }

        /// batches share one vertex upload.
        void recordBatches(std::span<const DebugBatch> batches, rhi::UploadRef vertices, rhi::UploadRef camera,
            const StylePipelines& pipelines, rhi::CommandList& commands)
        {
            for (const DebugDepth depth : DrawOrder) {
                for (const DebugBatch& batch : batches) {
                    if (batch.style.depth != depth || batch.vertexCount == 0)
                        continue;
                    commands.SetPipeline(pipelines[DebugStyleIndex(batch.style)]);
                    commands.SetConstants(CameraConstantsSlot, camera);
                    commands.BindVertexBuffer(0, rhi::BufferSource::FromUpload(vertices));
                    commands.Draw(batch.vertexCount, 1, batch.firstVertex);
                }
            }
        }

        void recordView(const PassTargets& targets, size_t viewIndex, const StylePipelines& pipelines, PassContext& context)
        {
            const auto& frame = context.Frame();
            const auto& view = frame.sceneViews[viewIndex];
            const std::span batches { frame.debug.batches.data() + view.firstDebugBatch, view.debugBatchCount };
            const ViewGeometry geometry = buildViewGeometry(batches, frame.debug.vertices, view.camera.viewProjection);
            if (geometry.batches.empty())
                return;

            auto& uploads = context.Uploads();
            const CameraConstants constants { view.camera.viewProjection };
            const auto camera = uploads.Upload(rhi::UploadArena::Constants, &constants, 1);
            const auto vertices = uploads.Upload(rhi::UploadArena::Vertex, geometry.vertices.data(), geometry.vertices.size());
            if (!camera.IsValid() || !vertices.IsValid()) {
                ERROR_MESSAGE(log::Category::Render, "DebugDrawPass", "Upload arena is full, %zu debug vertices skipped", geometry.vertices.size());
                return;
            }

            constexpr float unusedClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            auto& commands = context.Commands();
            commands.BeginRenderPass(context.ColorTarget(targets.color, false, unusedClearColor), context.DepthTarget(targets.depth, false));
            recordBatches(geometry.batches, vertices, camera, pipelines, commands);
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
            graph.AddPass<PassTargets>(
                "Debug Draw" + suffix,
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
