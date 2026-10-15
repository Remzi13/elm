#pragma once

#include "script/ui/ScriptWindowActions.hpp"

namespace elm::script {

    class HotReload;
    class MainModule;
    class ModuleLoader;

    /// The Modules tab of the Scripts window: the loaded modules, their state or error, reload
    /// counts, Reload and Edit. Owner: ScriptWindow, thread: update (UI).
    class ModulesPanel {
    public:
        /// What the panel hands to the rest of the window.
        struct Actions {
            PrintLine print;
            OpenModule edit;
            OpenLocation editLocation;
        };

        ModulesPanel(const ModuleLoader& loader, HotReload& hotReload, const MainModule& mainModule, Actions actions);

        void Draw();

    private:
        const ModuleLoader& m_loader;
        HotReload& m_hotReload;
        const MainModule& m_mainModule;
        Actions m_actions;
    };

} // namespace elm::script
