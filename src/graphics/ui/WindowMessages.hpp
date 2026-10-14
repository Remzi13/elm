#pragma once

#include "graphics/Model.hpp"

#include <cstddef>
#include <functional>

namespace elm {

	/// Provided by the model editor: opens a model of the scene ModelManager for editing.
	struct OpenModelEditor {
		std::function<void(graphics::ModelHandler)> open;
	};

	struct SelectedSceneInstance {
		size_t index;
	};

	/// Provided by the Inspector (scripts): Edit or Play mode of the engine, see ScriptSystem::Play.
	struct PlayControls {
		bool playing;
		/// Play in Edit mode, Stop in Play mode; applied on the next update.
		std::function<void()> toggle;
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
