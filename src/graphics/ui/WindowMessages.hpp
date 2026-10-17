#pragma once

#include "graphics/ModelHandler.hpp"

#include "Scene/NodeId.hpp"

#include <cstddef>
#include <functional>

namespace elm {

	/// Provided by the model editor: opens a model of the scene ModelManager for editing.
	struct OpenModelEditor {
		std::function<void(graphics::ModelHandler)> open;
	};

	/// Provided by Scene Hierarchy: the node it shows; an invalid id while the scene is empty.
	struct SelectedSceneInstance {
		NodeId node;
	};

	/// Provided by the Inspector (scripts): Edit or Play mode of the engine, see script::PlayMode.
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
