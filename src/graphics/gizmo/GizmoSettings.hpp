#pragma once

namespace elm {

    enum class GizmoOperation {
        Translate,
        Rotate,
        Scale
    };

    enum class GizmoSpace {
        World,
        Local
    };

    /// The gizmo mode the viewport window picks: what the handles do and in which axes.
    struct GizmoSettings {
        GizmoOperation operation;
        GizmoSpace space;
    };

} // namespace elm
