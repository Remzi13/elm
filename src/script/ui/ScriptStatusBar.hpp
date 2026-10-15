#pragma once

#include "script/ui/ScriptWindowActions.hpp"

namespace elm {
    class Settings;
}

namespace elm::script {

    namespace lua {
        class State;
    }
    class EntityScripts;
    class HotReload;
    class MainModule;

    /// The bar on top of the Scripts window: the state of the main module and of the entity scripts,
    /// Lua memory, hot reload, Reload changed, Rebuild scene, Edit main, Restart.
    /// Owner: ScriptWindow, thread: update (UI).
    class ScriptStatusBar {
    public:
        /// The script parts the bar shows; they outlive it.
        struct Parts {
            const lua::State& state;
            MainModule& mainModule;
            const EntityScripts& entityScripts;
            HotReload& hotReload;
        };
        /// What the bar hands to the rest of the window.
        struct Actions {
            PrintLine print;
            OpenModule edit;
            OpenLocation editLocation;
            /// Restarts the scripts on the next update.
            std::function<void()> requestRestart;
        };

        ScriptStatusBar(Settings& settings, Parts parts, Actions actions);

        void Draw();
        /// main.build_scene(), the result goes to the console.
        void RebuildScene();
        void RequestRestart();

    private:
        void DrawState();
        void DrawButtons();
        void DrawMainError();

        Settings& m_settings;
        Parts m_parts;
        Actions m_actions;
    };

} // namespace elm::script
