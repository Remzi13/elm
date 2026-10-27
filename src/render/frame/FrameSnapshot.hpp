#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"
#include "math/Primitivs.hpp"
#include "math/Vector.hpp"

#include "render/OverlayFrame.hpp"
#include "render/api/Handles.hpp"
#include "render/api/ResourceCommands.hpp"
#include "render/frame/DebugGeometry.hpp"

namespace elm::render {

    struct CameraData {
        Matrix4x4 viewProjection;
        Vector3 position;
    };

    /// One instance of a mesh. The layout is uploaded as-is into the instance vertex stream.
    struct InstanceData {
        Matrix4x4 transform;
        Vector4 color;
    };

    struct DrawItem {
        MeshHandle mesh;
        InstanceData instance;
    };

    /// 3D view rendered into an offscreen target that the UI displays.
    struct SceneView {
        CameraData camera;
        TextureHandle colorTarget;
        Size size;
        /// Items of this view: FrameSnapshot::drawItems[firstDraw, firstDraw + drawCount).
        size_t firstDraw { 0 };
        size_t drawCount { 0 };
        /// Debug batches of this view: FrameSnapshot::debug.batches[firstDebugBatch, firstDebugBatch + debugBatchCount).
        size_t firstDebugBatch { 0 };
        size_t debugBatchCount { 0 };

        [[nodiscard]] bool IsValid() const noexcept { return colorTarget.IsValid() && !size.IsEmpty(); }
    };

    /// Everything the render thread needs to draw one frame. Built by the update thread through
    /// FrameWriter, then owned by the render thread until it finishes the frame.
    struct FrameSnapshot {
        uint64_t frameIndex { 0 };
        Size backbufferSize;
        Vector<SceneView> sceneViews;
        Vector<DrawItem> drawItems;
        /// Debug primitives of all views; each view owns a range of the batches.
        DebugGeometry debug;
        OverlayFrame overlay;
        /// Executed before anything of this frame is drawn.
        ResourceCommandList resourceCommands;

        /// Keeps the allocated capacity, so steady-state frames do not allocate.
        void Reset()
        {
            frameIndex = 0;
            backbufferSize = {};
            sceneViews.clear();
            drawItems.clear();
            debug.Clear();
            overlay.Clear();
            resourceCommands.Clear();
        }
    };

} // namespace elm::render
