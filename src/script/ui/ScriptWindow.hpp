#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/WindowMessages.hpp"

#include "script/EngineContext.hpp"

#include "script/ui/ApiPanel.hpp"
#include "script/ui/ConsolePanel.hpp"
#include "script/ui/ModulesPanel.hpp"
#include "script/ui/ScriptEditor.hpp"
#include "script/ui/ScriptStatusBar.hpp"

namespace elm::script {

    class EntityScripts;
    class ScriptDefaults;

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
        ScriptDefaults& defaults;
        /// Restarts the scripts on the next update.
        std::function<void()> requestRestart;
        /// Edit/Play switches, given to the viewport as PlayControls.
        PlayRequests play;
    };

    /// Editor window for scripts. It lays out its parts and runs the console's /commands:
    ///  - the status bar on top (ScriptStatusBar);
    ///  - Console: Lua lines against the live engine, history, Tab completion (ConsolePanel);
    ///  - Editor: the script files, Ctrl+S saves and reloads the module (ScriptEditor);
    ///  - Modules: loaded modules, their state and errors, reload buttons (ModulesPanel);
    ///  - API: everything the bindings expose, a click puts it into the console (ApiPanel).
    /// It also provides the other windows with the scripts' side of the editor: PlayControls (Edit and Play)
    /// and ScriptInspection (props and instances for Scene Hierarchy).
    /// Owner: ImGuiSystem (made by ScriptSystem::CreateEditorWindows), thread: update (UI).
    class ScriptWindow final : public IImGuiWindow {
    public:
        ScriptWindow(Settings& settings, const ScriptWindowDesc& desc);

        [[nodiscard]] StringView GetName() const override { return "Scripts"; }
        void Render(ImGuiWindowContext& context) override;

    protected:
        void OnAttach() override;

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
        [[nodiscard]] ScriptInspection MakeInspection();
        [[nodiscard]] PlayControls MakePlayControls() const;

        const ModuleLoader& m_loader;
        HotReload& m_hotReload;
        EntityScripts& m_entityScripts;
        ScriptDefaults& m_defaults;
        PlayRequests m_play;
        const MainModule& m_mainModule;

        ConsolePanel m_console;
        ScriptEditor m_editor;
        ModulesPanel m_modules;
        ApiPanel m_api;
        ScriptStatusBar m_statusBar;
        bool m_selectConsole { false };
        bool m_selectEditor { false };
        bool m_focusRequested { false };
        Vector<MessageBus::Subscription> m_querySubscriptions;
    };

} // namespace elm::script
