#pragma once

#include "render/passes/IRenderFeature.hpp"

namespace elm::render {

    /// Draws the captured UI: one graph pass per surface (main window and every secondary UI
    /// window). The passes are independent of each other, so they are recorded in parallel.
    class OverlayPass final : public IRenderFeature {
    public:
        [[nodiscard]] StringView GetName() const override { return "Overlay"; }
        bool Init(rhi::IRenderBackend& backend) override;
        void Release(rhi::IRenderBackend& backend) override;
        void Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
            FrameBlackboard& blackboard) override;

    private:
        PipelineHandle m_pipeline;
    };

} // namespace elm::render
