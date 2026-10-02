#pragma once

#include <cstddef>

namespace elm {

	struct SelectedSceneInstance {
		size_t index;
	};

	enum class GizmoOperation {
		Translate,
		Rotate,
		Scale
	};

	enum class GizmoSpace {
		World,
		Local
	};

	struct GizmoSettings {
		GizmoOperation operation;
		GizmoSpace space;
	};

} // namespace elm
