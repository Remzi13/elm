#pragma once

#include "core/Std.hpp"

#include "graphics/gizmo/TransformGizmo.hpp"
#include "editor/ui/IImGuiWindow.hpp"
#include "editor/ui/EditorServices.hpp"

#include "script/EngineContext.hpp"

#include "imgui.h"

#include <optional>
namespace elm {

    class Camera;
    class ModelCatalog;
    class Scene;
    class SceneEdits;

    namespace render {
        class ViewPort;
    }

    /// What the viewport window works with; it outlives projects, so do these (Editor).
    struct EngineViewportDeps {
        Scene& scene;
        /// The models of the nodes: their bounds are what a click picks and F brings into the view.
        const ModelCatalog& models;
        /// The view camera: the window sets its aspect to the image; in Edit the wheel and F move it.
        Camera& camera;
        /// The engine view: the window sizes it to the image and shows it.
        render::ViewPort& viewPort;
        graphics::DebugDraw& debugDraw;
        /// In Play the keys are the game's.
        const script::PlayRequests& play;
        /// The node the gizmo moves; a click on the view writes it.
        Selection& selection;
        /// The gizmo mode: the view menu and the keys W, E, R pick it.
        GizmoSettings& gizmo;
        /// Ctrl+Z and Ctrl+Y while the viewport has the focus.
        SceneEdits& edits;
        /// Ctrl+S while the viewport has the focus.
        const SceneFileRequests& sceneFile;
    };

    /// The engine view and what is done in it. The image fills the window; in its top left corner lies the view menu,
    /// an arrow that opens into sections (the gizmo mode, also W, E, R; the debug draw channels) on a translucent
    /// background. A new control of the view becomes a section of it. In Edit a left click selects the node under the mouse (nothing on empty space), F brings the
    /// selected node into the view, the wheel moves the camera along its view; in Play the mouse and the keys are
    /// the game's. The image carries the debug labels and the gizmo of the selected node.
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
        /// Over the image, from its corner. Returns whether the mouse is over the menu: the view is not under it.
        [[nodiscard]] bool DrawViewMenu(ImVec2 corner);
        void DrawGizmoSection();
        void DrawDebugSection();
        /// W, E, R and F. Not with the right mouse button: the fly camera has WASD and QE then.
        void HandleKeys();
        /// Moves the selected node with the gizmo and draws the gizmo.
        void UpdateGizmo(NodeId node, const graphics::GizmoInput& input);
        /// The mouse over the image in Edit: a click selects, the wheel moves the camera.
        void HandleMouse(const graphics::GizmoInput& input);

        EngineViewportDeps m_deps;

        /// The view menu shows its sections; closed, it is one arrow.
        bool m_menuOpen { false };
        bool m_debugSectionOpen { false };
        bool m_hovered { false };
        bool m_focused { false };
        /// Made on the first frame with a selection: it registers its debug draw channel.
        std::optional<graphics::TransformGizmo> m_gizmo;
    };

} // namespace elm
