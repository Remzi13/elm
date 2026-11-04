#include "editor/script/ScriptEditorWindows.hpp"

#include "script/ScriptSystem.hpp"
#include "script/entity/EntityScripts.hpp"
#include "script/entity/ScriptDefaults.hpp"
#include "script/reload/HotReload.hpp"

#include "editor/script/ScriptWindow.hpp"

namespace elm::script {

    /// The parts of a ScriptSystem the Scripts window shows: it is their friend.
    struct EditorWindowsAccess {
        static ScriptWindowDesc MakeWindowDesc(ScriptSystem& system)
        {
            return { system.m_state, *system.m_console, *system.m_modulePaths, *system.m_moduleLoader, *system.m_hotReload,
                *system.m_mainModule, *system.m_entityScripts, [&system] { system.m_restartRequested = true; } };
        }

        static EntityScripts& Entities(ScriptSystem& system) { return *system.m_entityScripts; }
        static ScriptDefaults& Defaults(ScriptSystem& system) { return *system.m_scriptDefaults; }
        static HotReload& Reload(ScriptSystem& system) { return *system.m_hotReload; }
    };

    UniquePtr<ScriptWindow> CreateScriptWindow(ScriptSystem& scripts, Settings& settings)
    {
        return MakeUnique<ScriptWindow>(settings, EditorWindowsAccess::MakeWindowDesc(scripts));
    }

    ScriptInspection MakeScriptInspection(ScriptSystem& scripts)
    {
        EntityScripts& entities = EditorWindowsAccess::Entities(scripts);
        ScriptDefaults& defaults = EditorWindowsAccess::Defaults(scripts);
        HotReload& reload = EditorWindowsAccess::Reload(scripts);
        return ScriptInspection {
            .defaults = [&defaults](StringView module) { return defaults.Get(module); },
            .forgetDefaults = [&defaults] { defaults.Forget(); },
            .instance =
                [&entities](NodeId node) {
                    const EntityScript* instance = entities.Find(node);
                    if (!instance)
                        return ScriptInstanceState {};
                    if (instance->faulted)
                        return ScriptInstanceState { ScriptInstanceState::Kind::Faulted, instance->error, instance->module };
                    ScriptInstanceState state;
                    state.kind = instance->started ? ScriptInstanceState::Kind::Running : ScriptInstanceState::Kind::Starting;
                    return state;
                },
            .setField = [&entities](NodeId node, StringView name, const ScriptValue& value) { entities.SetField(node, name, value); },
            .endInstance = [&entities](NodeId node) { entities.Destroy({ &node, 1 }); },
            .reloadModule = [&reload](StringView module) { (void)reload.Reload(module); },
        };
    }

} // namespace elm::script
