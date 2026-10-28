#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/ModelHandler.hpp"

#include "Scene/NodeId.hpp"
#include "Scene/ScriptComponent.hpp"

#include <cstddef>
#include <functional>

namespace elm {

	/// Provided by the model editor: opens a model of the scene ModelManager for editing.
	struct OpenModelEditor {
		std::function<void(graphics::ModelHandler)> open;
	};

	/// Provided by Scene Hierarchy: replaces the scene with a scene file (the Project window opens one).
	struct OpenSceneFile {
		std::function<void(const Path& file)> open;
	};

	/// Provided by the model editor: opens a .model file for editing.
	struct OpenModelFile {
		std::function<void(const Path& file)> open;
	};

	/// Provided by the Scripts window: opens a script file in its editor.
	struct OpenScriptFile {
		std::function<void(const Path& file)> open;
	};

	/// ImGui drag and drop payload of a model file dragged from the Project window: its absolute path, a string.
	inline constexpr const char* ModelFilePayload = "ELM_MODEL_FILE";

	/// The props a script declares, with their defaults, by name.
	using ScriptProps = Vector<std::pair<String, ScriptValue>>;

	/// How the script instance of a node is doing (Play mode only has instances).
	struct ScriptInstanceState {
		enum class Kind : uint8_t {
			None,
			Starting,
			Running,
			/// Stopped by an error until its module reloads.
			Faulted
		};
		Kind kind { Kind::None };
		/// Faulted: the error and the module whose reload starts the instance again.
		String error;
		String module;
	};

	/// Provided by the Scripts window: what Scene Hierarchy shows and changes of the Lua scripts of a node.
	struct ScriptInspection {
		/// The props a module declares, with their defaults; the error when the module cannot be read.
		std::function<EngineResult<ScriptProps>(StringView module)> defaults;
		/// Reads the modules' props again, after an error in them was fixed.
		std::function<void()> forgetDefaults;
		std::function<ScriptInstanceState(NodeId node)> instance;
		/// Writes a prop into the running instance too (Play mode); without one nothing happens.
		std::function<void(NodeId node, StringView name, const ScriptValue& value)> setField;
		/// Ends the node's instance (on_destroy): before its Script component is removed.
		std::function<void(NodeId node)> endInstance;
		std::function<void(StringView module)> reloadModule;
	};

	/// Provided by Scene Hierarchy: the node it shows; an invalid id while the scene is empty.
	struct SelectedSceneInstance {
		NodeId node;
	};

	/// Provided by the Scripts window: Edit or Play mode of the engine, see script::PlayMode.
	/// The switches are requests, applied on the next update.
	struct PlayControls {
		bool playing;
		/// Play as the game starts: the startup scene and the main module of the project.
		std::function<void()> playProject;
		/// Play the edited scene with the main module.
		std::function<void()> playScene;
		std::function<void()> stop;
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
