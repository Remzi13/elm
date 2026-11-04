#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "model/ModelHandler.hpp"

#include "Scene/NodeId.hpp"
#include "Scene/ScriptComponent.hpp"

#include <cstddef>
#include <functional>

namespace elm {

    /// The node Scene Hierarchy marks, Inspector shows and the viewport's gizmo moves. Invalid when nothing is
    /// selected; a node that went finds nothing, like any stale NodeId. Owner: Editor. Thread: update (UI).
    struct Selection {
        NodeId node;
    };

    /// Opens files and models in the window that edits them. Editor fills it when it makes the project
    /// windows and empties it before they close: an empty function opens nothing. Thread: update (UI).
    struct FileOpener {
        /// Replaces the scene with a scene file, asking first when it has unsaved changes.
        std::function<void(const Path& file)> scene;
        /// Model Editor: a .model file.
        std::function<void(const Path& file)> model;
        /// Model Editor: a model of the scene ModelCatalog.
        std::function<void(ModelHandler model)> sceneModel;
        /// The Scripts window: a script file in its editor.
        std::function<void(const Path& file)> script;
    };

    /// What a window asks of the open scene's file by a key. Editor fills it while a project is open: an empty
    /// function does nothing. Thread: update (UI).
    struct SceneFileRequests {
        std::function<void()> save;
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

    /// Made from the script system (script::MakeScriptInspection): what Scene Hierarchy shows and changes of the
    /// Lua scripts of a node. Valid while the script system lives.
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

} // namespace elm
