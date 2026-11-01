#include "graphics/debug/DebugDrawing.hpp"

#include "core/Debug.hpp"

namespace elm::graphics {

DebugDrawing::DebugDrawing(Settings& settings)
    : m_debugDraw(settings)
{
}

void DebugDrawing::AddVisualizer(UniquePtr<IDebugVisualizer> visualizer)
{
    ELM_ASSERT(visualizer && "a visualizer is required");
    m_visualizers.push_back(std::move(visualizer));
}

void DebugDrawing::BeginFrame(float deltaSeconds)
{
    m_debugDraw.BeginFrame(deltaSeconds);
}

void DebugDrawing::EndFrame(render::FrameWriter& frame)
{
    ELM_ASSERT_THREAD(Update);
    for (const auto& visualizer : m_visualizers)
        visualizer->Draw(m_debugDraw);
    m_debugDraw.WriteTo(frame);
    m_debugDraw.PublishLabels();
}

} // namespace elm::graphics
