#pragma once

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm {
class Scene;
}

namespace elm::graphics {

/// Draws the shape of every Collider component as a wireframe: solid colliders and triggers in
/// their own channels. The shapes come from the components, not from physics bodies, so they
/// show in Edit too. Reads the scene only; update thread. The scene must outlive the visualizer.
class ColliderVisualizer final : public IDebugVisualizer {
public:
    ColliderVisualizer(const Scene& scene, DebugDraw& debugDraw);

    void Draw(DebugDraw& debugDraw) override;

private:
    const Scene& m_scene;
    DebugChannel m_collidersChannel;
    DebugChannel m_triggersChannel;
};

} // namespace elm::graphics
