#include "graphics/debug/DebugDrawSystem.hpp"

#include "core/Debug.hpp"

namespace elm::graphics {

DebugDrawSystem::DebugDrawSystem(Settings& settings)
    : m_debugDraw(settings)
{
}

void DebugDrawSystem::AddVisualizer(UniquePtr<IDebugVisualizer> visualizer)
{
    ELM_ASSERT(visualizer && "a visualizer is required");
    m_visualizers.push_back(std::move(visualizer));
}

void DebugDrawSystem::BeginFrame(float deltaSeconds)
{
    m_debugDraw.BeginFrame(deltaSeconds);
}

void DebugDrawSystem::EndFrame(render::FrameWriter& frame)
{
    ELM_ASSERT_THREAD(Update);
    for (const auto& visualizer : m_visualizers)
        visualizer->Draw(m_debugDraw);
    m_debugDraw.WriteTo(frame);
    m_debugDraw.PublishLabels();
}

} // namespace elm::graphics
