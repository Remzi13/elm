#pragma once

#include "core/Std.hpp"

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm::render {
    class FrameWriter;
}

namespace elm::graphics {

    /// The debug drawing of one scene view: the DebugDraw every source of the frame writes into
    /// (systems, scripts, editor windows) and the visualizers drawn into it once the scene is final.
    /// Owned by the owner of the view (for the engine viewport the program: Editor, elm built with
    /// ELM_DEBUG_DRAW); update thread only.
    class DebugDrawing {
    public:
        explicit DebugDrawing(Settings& settings);

        DebugDrawing(const DebugDrawing&) = delete;
        DebugDrawing& operator=(const DebugDrawing&) = delete;
        DebugDrawing(DebugDrawing&&) = delete;
        DebugDrawing& operator=(DebugDrawing&&) = delete;

        /// What the sources draw into; valid for the life of the system.
        [[nodiscard]] DebugDraw& GetDebugDraw() noexcept { return m_debugDraw; }
        /// Drawn every frame from now on, in the order added.
        void AddVisualizer(UniquePtr<IDebugVisualizer> visualizer);

        /// Starts a frame, before anything of it draws (physics steps included).
        void BeginFrame(float deltaSeconds);
        /// Ends the frame once the scene is final: draws the visualizers, adds every primitive to the
        /// current scene view of frame and publishes the labels for the UI of the next frame.
        void EndFrame(render::FrameWriter& frame);

    private:
        DebugDraw m_debugDraw;
        Vector<UniquePtr<IDebugVisualizer>> m_visualizers;
    };

} // namespace elm::graphics
