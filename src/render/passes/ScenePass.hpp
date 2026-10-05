#pragma once

#include "render/passes/IRenderFeature.hpp"

namespace elm::render {

    /// Draws the 3D scene (opaque meshes, one instanced draw per mesh) into the scene view target.
    class ScenePass final : public IRenderFeature {
    public:
        [[nodiscard]] StringView GetName() const override { return "Scene"; }
        bool Init(rhi::IRenderBackend& backend) override;
        void Release(rhi::IRenderBackend& backend) override;
        void Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
            FrameBlackboard& blackboard) override;

    private:
        PipelineHandle m_pipeline;
    };

} // namespace elm::render
