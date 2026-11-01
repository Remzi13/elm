#include "editor/script/ScriptEditorWindows.hpp"

#include "script/ScriptSystem.hpp"
#include "editor/script/ScriptWindow.hpp"

namespace elm::script {

    /// The parts of a ScriptSystem the Scripts window shows: it is their friend.
    struct EditorWindowsAccess {
        static ScriptWindowDesc MakeWindowDesc(ScriptSystem& system)
        {
            return { system.m_state, *system.m_console, *system.m_modulePaths, *system.m_moduleLoader, *system.m_hotReload,
                *system.m_mainModule, *system.m_entityScripts, *system.m_scriptDefaults,
                [&system] { system.m_restartRequested = true; }, system.m_engine.play };
        }
    };

    Vector<UniquePtr<IImGuiWindow>> CreateEditorWindows(ScriptSystem& scripts, Settings& settings)
    {
        Vector<UniquePtr<IImGuiWindow>> windows;
        windows.push_back(MakeUnique<ScriptWindow>(settings, EditorWindowsAccess::MakeWindowDesc(scripts)));
        return windows;
    }

} // namespace elm::script
