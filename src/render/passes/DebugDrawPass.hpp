#pragma once

#include "render/frame/DebugGeometry.hpp"
#include "render/passes/IRenderFeature.hpp"

#include <array>

namespace elm::render {

    /// Draws the debug geometry of every scene view over it, after ScenePass. Primitives with
    /// DebugDepth::Tested are hidden by the scene depth, which this pass reads and never writes.
    class DebugDrawPass final : public IRenderFeature {
    public:
        [[nodiscard]] StringView GetName() const override { return "Debug Draw"; }
        bool Init(rhi::IRenderBackend& backend) override;
        void Release(rhi::IRenderBackend& backend) override;
        void Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
            FrameBlackboard& blackboard) override;

    private:
        /// Indexed by DebugStyleIndex.
        std::array<PipelineHandle, AllDebugStyles.size()> m_pipelines {};
    };

} // namespace elm::render
