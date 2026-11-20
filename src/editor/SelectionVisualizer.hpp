#pragma once

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm {

    class CameraPilot;
    class ModelCatalog;
    class Scene;
    class Selection;

    /// What SelectionVisualizer shows. All of it outlives the visualizer.
    struct SelectionVisualizerDeps {
        const Scene& scene;
        const ModelCatalog& models;
        const Selection& selection;
        /// The piloted camera is where the view is: its box would surround the eye.
        const CameraPilot& pilot;
    };

    /// The world bounds of the selected nodes (NodeBounds), and around several of them the bounds of them all, drawn
    /// over the scene. Channel Editor/Selection. Owner: Editor's DebugDrawing. Thread: update.
    class SelectionVisualizer final : public graphics::IDebugVisualizer {
    public:
        SelectionVisualizer(const SelectionVisualizerDeps& deps, graphics::DebugDraw& debugDraw);

        void Draw(graphics::DebugDraw& debugDraw) override;

    private:
        SelectionVisualizerDeps m_deps;
        graphics::DebugChannel m_channel;
    };

} // namespace elm
