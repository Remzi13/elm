#pragma once

#include "core/Std.hpp"

#include "render/graph/RenderGraph.hpp"

namespace elm::render {

    namespace rhi {
        class IRenderBackend;
    }

    struct FrameSnapshot;

    /// Graph resources features hand to each other within one frame.
    struct FrameBlackboard {
        /// Rendered scene view, sampled by the UI.
        RGTexture sceneColor;
    };

    /// A rendering feature: owns its pipelines and adds its passes to the frame graph.
    /// Render thread only. Setup must not record commands, only declare passes.
    class IRenderFeature {
    public:
        virtual ~IRenderFeature() = default;

        [[nodiscard]] virtual StringView GetName() const = 0;
        virtual bool Init(rhi::IRenderBackend& backend) = 0;
        virtual void Release(rhi::IRenderBackend& backend) = 0;
        virtual void Setup(RenderGraph& graph, const FrameSnapshot& frame, const rhi::IRenderBackend& backend,
            FrameBlackboard& blackboard) = 0;
    };

} // namespace elm::render
