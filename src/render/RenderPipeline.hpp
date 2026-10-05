#pragma once

#include "core/Std.hpp"

#include "render/graph/RenderGraph.hpp"
#include "render/passes/IRenderFeature.hpp"
#include "render/rhi/UploadAllocator.hpp"

namespace elm::render {

    namespace rhi {
        class IRenderBackend;
    }

    struct FrameSnapshot;

    /// Turns a frame snapshot into GPU work: every feature adds its passes to a fresh render graph,
    /// which is compiled, recorded and submitted. Render thread only.
    class RenderPipeline {
    public:
        RenderPipeline();
        ~RenderPipeline();

        RenderPipeline(const RenderPipeline&) = delete;
        RenderPipeline& operator=(const RenderPipeline&) = delete;

        bool Init(rhi::IRenderBackend& backend);
        void Release(rhi::IRenderBackend& backend);

        void Render(rhi::IRenderBackend& backend, const FrameSnapshot& frame);

        /// Record independent passes and pass batches on the job system (default: on).
        void SetParallelRecording(bool enabled) noexcept { m_parallelRecording = enabled; }
        [[nodiscard]] const RenderGraph::Stats& GetGraphStats() const noexcept { return m_graph.GetStats(); }

    private:
        Vector<UniquePtr<IRenderFeature>> m_features;
        RenderGraph m_graph;
        TransientTexturePool m_transientTextures;
        UniquePtr<rhi::UploadAllocator> m_uploads;
        bool m_parallelRecording { true };
    };

} // namespace elm::render
