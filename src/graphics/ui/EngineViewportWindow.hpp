#pragma once

#include "core/Std.hpp"

#include "graphics/gizmo/TransformGizmo.hpp"
#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/WindowMessages.hpp"

#include <optional>
namespace elm  {

class EngineViewportWindow final : public IImGuiWindow {
public:
    using IImGuiWindow::IImGuiWindow;

    [[nodiscard]] StringView GetName() const override { return "Engine Viewport"; }
    void Render(ImGuiWindowContext& context) override;

    // State of the last built frame, used to route camera input to the viewport only
    [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
    [[nodiscard]] bool IsFocused() const noexcept { return m_focused; }

private:
    /// Moves the selected node with the gizmo and draws the gizmo.
    void UpdateGizmo(ImGuiWindowContext& context, NodeId node, const GizmoSettings& settings, const graphics::GizmoInput& input);

    bool m_hovered { false };
    bool m_focused { false };
    /// Made on the first frame with a selection: it registers its debug draw channel.
    std::optional<graphics::TransformGizmo> m_gizmo;
};

} // namespace Engine
