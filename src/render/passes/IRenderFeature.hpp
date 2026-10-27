#pragma once

#include "core/Std.hpp"

#include "render/graph/RenderGraph.hpp"

namespace elm::render {

    namespace rhi {
        class IRenderBackend;
    }

    struct FrameSnapshot;

    /// Targets of one rendered scene view. Passes that draw over the scene write into them after ScenePass.
    struct SceneViewTargets {
        /// Formats of the targets: pipelines drawing into a scene view are created with them.
        static constexpr TextureFormat ColorFormat = TextureFormat::RGBA8_UNORM_SRGB;
        static constexpr TextureFormat DepthFormat = TextureFormat::D32_FLOAT;

        /// Index into FrameSnapshot::sceneViews.
        size_t viewIndex { 0 };
        RGTexture color;
        /// Scene depth of this view, valid until the end of the frame.
        RGTexture depth;
    };

    /// Graph resources features hand to each other within one frame.
    struct FrameBlackboard {
        /// Rendered scene views; their color targets are sampled by the UI.
        Vector<SceneViewTargets> sceneViews;
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
