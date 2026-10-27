#pragma once

#include "graphics/Camera.hpp"
#include "graphics/debug/DebugDraw.hpp"
#include "graphics/ui/WindowMessages.hpp"

#include "math/Matrix.hpp"
#include "math/Vector.hpp"

#include <array>
#include <optional>

namespace elm::graphics {

    /// The mouse over the 3D view in one frame, as the gizmo sees it.
    struct GizmoInput {
        /// From the camera through the mouse.
        ViewRay ray;
        math::Vector3 cameraPosition;
        /// The mouse is over the view: only then a handle can be hovered.
        bool overView { false };
        /// The left button went down over the view this frame.
        bool pressed { false };
        /// The left button is held: a drag goes on outside the view.
        bool down { false };
    };

    /// A handle of the gizmo: the axis it moves, rotates or scales along.
    enum class GizmoAxis : uint8_t { None, X, Y, Z };

    /// Move, rotate and scale handles of a world transform, drawn with DebugDraw over the scene
    /// (channel Editor/Gizmo) and dragged with the left mouse button. Keeps the drag between
    /// frames; the transform belongs to the caller. Editor UI on the update thread.
    /// Scale always goes along the local axes; move and rotate follow GizmoSettings::space.
    class TransformGizmo {
    public:
        static constexpr StringView ChannelName = "Editor/Gizmo";

        explicit TransformGizmo(DebugDraw& debugDraw);

        [[nodiscard]] DebugChannel Channel() const noexcept { return m_channel; }
        [[nodiscard]] bool IsDragging() const noexcept { return m_drag.has_value(); }
        /// The handle under the mouse after the last Update, None while dragging.
        [[nodiscard]] GizmoAxis GetHovered() const noexcept { return m_hovered; }

        /// Places the handles on world, finds the hovered one and drags it. While a drag goes on,
        /// returns the new world transform for the caller to apply.
        [[nodiscard]] std::optional<math::Matrix4x4> Update(const math::Matrix4x4& world, const GizmoSettings& settings,
            const GizmoInput& input);
        /// Draws the handles where the last Update placed them.
        void Draw(DebugDraw& debugDraw) const;
        /// Forgets the handles and the drag: nothing is selected.
        void Reset() noexcept;

    private:
        /// Where the handles are in one frame.
        struct Handles {
            math::Vector3 center;
            /// Unit length, X Y Z.
            std::array<math::Vector3, 3> axes;
            /// Length of a handle in world units, constant on screen.
            float size { 1.0f };
            GizmoOperation operation { GizmoOperation::Translate };
        };

        /// A drag in progress: everything is measured from where it began.
        struct Drag {
            GizmoAxis axis { GizmoAxis::None };
            math::Matrix4x4 startWorld;
            Handles startHandles;
            /// Move and scale: position of the mouse along the axis.
            float startParameter { 0.0f };
            /// Rotate: from the center to the mouse in the plane of the ring, unit length.
            math::Vector3 startDirection;
        };

        [[nodiscard]] GizmoAxis FindHovered(const ViewRay& ray) const;
        [[nodiscard]] std::optional<Drag> StartDrag(GizmoAxis axis, const math::Matrix4x4& world, const ViewRay& ray) const;
        [[nodiscard]] static std::optional<math::Matrix4x4> DragTo(const Drag& drag, const ViewRay& ray);

        DebugChannel m_channel;
        std::optional<Handles> m_handles;
        std::optional<Drag> m_drag;
        GizmoAxis m_hovered { GizmoAxis::None };
    };

} // namespace elm::graphics
