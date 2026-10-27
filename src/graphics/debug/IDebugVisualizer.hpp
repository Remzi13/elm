#pragma once

namespace elm::graphics {

    class DebugDraw;

    /// Draws one kind of debug visualization of some engine state every frame (colliders, bounds).
    /// Gets what it shows through its constructor and registers its channels there.
    /// Called on the update thread after the scene is final for the frame.
    class IDebugVisualizer {
    public:
        virtual ~IDebugVisualizer() = default;

        virtual void Draw(DebugDraw& debugDraw) = 0;
    };

} // namespace elm::graphics
