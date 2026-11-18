#pragma once

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm {
    class Camera;
    class Scene;
} // namespace elm

namespace elm::graphics {

    /// Draws small solid camera models with their view direction for Camera components, except the one the view
    /// stands in, and light shapes (direction arrows, range spheres, spot cones) for Light components.
    /// Owner: the program's DebugDrawing; the scene and the view camera outlive it. Thread: update.
    class CameraAndLightVisualizer final : public IDebugVisualizer {
    public:
        CameraAndLightVisualizer(const Scene& scene, const Camera& view, DebugDraw& debugDraw);

        void Draw(DebugDraw& debugDraw) override;

    private:
        const Scene& m_scene;
        const Camera& m_view;
        DebugChannel m_camerasChannel;
        DebugChannel m_lightsChannel;
    };

} // namespace elm::graphics
