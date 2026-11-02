#pragma once

#include "core/Std.hpp"

#include "graphics/gizmo/TransformGizmo.hpp"
#include "editor/ui/IImGuiWindow.hpp"
#include "editor/ui/EditorServices.hpp"

#include "script/EngineContext.hpp"

#include <optional>
namespace elm  {

class Camera;
class Scene;
class SceneEdits;

namespace render {
    class ViewPort;
}

/// What the viewport window works with; it outlives projects, so do these (Editor).
struct EngineViewportDeps {
    Scene& scene;
    /// The view camera: the window sets its aspect to the image.
    Camera& camera;
    /// The engine view: the window sizes it to the image and shows it.
    render::ViewPort& viewPort;
    graphics::DebugDraw& debugDraw;
    /// Play and Stop over the image.
    const script::PlayRequests& play;
    /// The node the gizmo moves.
    const Selection& selection;
    const GizmoSettings& gizmo;
    /// Ctrl+Z and Ctrl+Y while the viewport has the focus.
    SceneEdits& edits;
};

/// The engine view with Play/Stop, the debug channels, the debug labels and the gizmo of the selected node.
/// Owner: ImGuiSystem, thread: update (UI).
class EngineViewportWindow final : public IImGuiWindow {
public:
    EngineViewportWindow(Settings& settings, const EngineViewportDeps& deps);

    [[nodiscard]] StringView GetName() const override { return "Engine Viewport"; }
    void Render(render::FrameWriter& frame) override;

    // State of the last built frame, used to route camera input to the viewport only
    [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
    [[nodiscard]] bool IsFocused() const noexcept { return m_focused; }

private:
    /// Moves the selected node with the gizmo and draws the gizmo.
    void UpdateGizmo(NodeId node, const graphics::GizmoInput& input);

    EngineViewportDeps m_deps;

    bool m_hovered { false };
    bool m_focused { false };
    /// Made on the first frame with a selection: it registers its debug draw channel.
    std::optional<graphics::TransformGizmo> m_gizmo;
};

} // namespace Engine
