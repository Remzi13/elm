#include "editor/SelectionVisualizer.hpp"

#include "editor/CameraPilot.hpp"
#include "editor/NodeEditing.hpp"
#include "editor/ui/Selection.hpp"

#include "math/Primitivs.hpp"

namespace elm {

    namespace {

        constexpr StringView SelectionChannel = "Editor/Selection";
        constexpr math::Vector4 NodeBoxColor { 0.85f, 0.45f, 0.1f, 1.0f };
        constexpr math::Vector4 GroupBoxColor { 1.0f, 0.8f, 0.25f, 1.0f };

    } // namespace

    SelectionVisualizer::SelectionVisualizer(const SelectionVisualizerDeps& deps, graphics::DebugDraw& debugDraw)
        : m_deps(deps)
        , m_channel(debugDraw.RegisterChannel(SelectionChannel, "Selection", true))
    {
    }

    void SelectionVisualizer::Draw(graphics::DebugDraw& debugDraw)
    {
        const auto nodes = m_deps.selection.Nodes();
        for (const NodeId node : nodes) {
            const auto bounds = node != m_deps.pilot.Piloted() ? NodeBounds(m_deps.scene, m_deps.models, node) : std::nullopt;
            if (bounds)
                debugDraw.Box(m_channel, math::Matrix4x4::Translation(bounds->GetCenter()), bounds->GetExtent(), { NodeBoxColor });
        }
        // One node's box is the group's already
        const auto group = nodes.size() > 1 ? GroupBounds(m_deps.scene, m_deps.models, nodes) : std::nullopt;
        if (group)
            debugDraw.Box(m_channel, math::Matrix4x4::Translation(group->GetCenter()), group->GetExtent(), { GroupBoxColor, render::DebugDepth::OnTop });
    }

} // namespace elm
