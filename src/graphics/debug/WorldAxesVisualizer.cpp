#include "graphics/debug/WorldAxesVisualizer.hpp"

namespace elm::graphics {

namespace {

    constexpr StringView WorldAxesChannel = "Editor/WorldAxes";
    constexpr float WorldAxisLength = 2.0f;

} // namespace

WorldAxesVisualizer::WorldAxesVisualizer(DebugDraw& debugDraw)
    : m_channel(debugDraw.RegisterChannel(WorldAxesChannel, "World Axes", true))
{
}

void WorldAxesVisualizer::Draw(DebugDraw& debugDraw)
{
    debugDraw.Axes(m_channel, math::Matrix4x4::Identity(), WorldAxisLength, {});
}

} // namespace elm::graphics
