#pragma once

#include "graphics/debug/DebugDraw.hpp"
#include "graphics/debug/IDebugVisualizer.hpp"

namespace elm::graphics {

/// Draws the X, Y and Z axes of the world at its origin, in the channel Editor/WorldAxes.
/// Update thread.
class WorldAxesVisualizer final : public IDebugVisualizer {
public:
    explicit WorldAxesVisualizer(DebugDraw& debugDraw);

    void Draw(DebugDraw& debugDraw) override;

private:
    DebugChannel m_channel;
};

} // namespace elm::graphics
