#pragma once

#include "editor/ui/IImGuiWindow.hpp"

#include "editor/script/ApiPanel.hpp"
#include "editor/script/ConsolePanel.hpp"
#include "editor/script/ModulesPanel.hpp"
#include "editor/script/ScriptEditor.hpp"
#include "editor/script/ScriptStatusBar.hpp"

namespace elm::script {

    class EntityScripts;

    class ScriptConsole;

    /// The script parts the Scripts window works with; they outlive the window.
    struct ScriptWindowDesc {
        lua::State& state;
        ScriptConsole& console;
        const ModulePaths& paths;
        const ModuleLoader& loader;
        HotReload& hotReload;
        MainModule& mainModule;
        EntityScripts& entityScripts;
        /// Restarts the scripts on the next update.
        std::function<void()> requestRestart;
    };

    /// Editor window for scripts. It lays out its parts and runs the console's /commands:
    ///  - the status bar on top (ScriptStatusBar);
    ///  - Console: Lua lines against the live engine, history, Tab completion (ConsolePanel);
    ///  - Editor: the script files, Ctrl+S saves and reloads the module (ScriptEditor);
    ///  - Modules: loaded modules, their state and errors, reload buttons (ModulesPanel);
    ///  - API: everything the bindings expose, a click puts it into the console (ApiPanel).
    /// Owner: ImGuiSystem (made by script::CreateScriptWindow), thread: update (UI).
    class ScriptWindow final : public IImGuiWindow {
    public:
        ScriptWindow(Settings& settings, const ScriptWindowDesc& desc);

        [[nodiscard]] StringView GetName() const override { return "Scripts"; }
        void Render(render::FrameWriter& frame) override;
        /// Opens a script file in the Editor tab (FileOpener::script).
        void OpenFile(const Path& file);

    private:
        void DrawTabs();
        void RunCommand(StringView command);
        void Reload(StringView module);
        void ListModules();
        void Print(ConsoleLineKind kind, StringView text);
        void InsertIntoConsole(StringView text);
        /// Opens a file in the Editor tab and switches to it.
        void Edit(StringView module, int line = 0);
        void EditLocation(StringView message);

        const ModuleLoader& m_loader;
        HotReload& m_hotReload;
        const MainModule& m_mainModule;

        ConsolePanel m_console;
        ScriptEditor m_editor;
        ModulesPanel m_modules;
        ApiPanel m_api;
        ScriptStatusBar m_statusBar;
        bool m_selectConsole { false };
        bool m_selectEditor { false };
        bool m_focusRequested { false };
    };

} // namespace elm::script
