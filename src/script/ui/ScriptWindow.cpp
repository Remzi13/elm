#include "script/ui/ScriptWindow.hpp"

#include "graphics/ui/WindowMessages.hpp"

#include "script/entity/EntityScripts.hpp"
#include "script/entity/ScriptDefaults.hpp"

#include "script/module/MainModule.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/reload/HotReload.hpp"

#include "imgui.h"

namespace elm::script {

    namespace {

        constexpr const char* HelpText =
            "Enter runs a Lua line against the live engine, an expression prints its value.\n"
            "Tab completes names (elm.sc -> elm.scene), Up/Down walk the history.\n"
            "  elm.scene / elm.models / elm.camera / elm.input / elm.math   engine API (see the API tab)\n"
            "  elm.engine.load_test_scene(1), clear_scene(), save_scene(f), load_scene(f)\n"
            "  elm.script.reload(name), reload_changed(), restart(), persist(key, default)\n"
            "Console commands:\n"
            "  /help  /clear  /modules  /reload [module]  /restart\n"
            "  /scene            rebuild the scene: main.build_scene()\n"
            "  /edit [module]    open a module (main by default) in the Editor tab";

        StringView Trim(StringView text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.remove_suffix(1);
            return text;
        }

    } // namespace

    ScriptWindow::ScriptWindow(Settings& settings, const ScriptWindowDesc& desc)
        : IImGuiWindow(settings)
        , m_loader(desc.loader)
        , m_hotReload(desc.hotReload)
        , m_entityScripts(desc.entityScripts)
        , m_defaults(desc.defaults)
        , m_play(desc.play)
        , m_mainModule(desc.mainModule)
        , m_console(desc.console, [this](StringView command) { RunCommand(command); })
        , m_editor(desc.paths, desc.loader, desc.hotReload, desc.mainModule)
        , m_modules(desc.loader, desc.hotReload, desc.mainModule,
              { [this](ConsoleLineKind kind, StringView text) { Print(kind, text); }, [this](StringView module) { Edit(module); },
                  [this](StringView message) { EditLocation(message); } })
        , m_api(desc.state, [this](StringView text) { InsertIntoConsole(text); })
        , m_statusBar(settings, { desc.state, desc.mainModule, desc.entityScripts, desc.hotReload },
              { [this](ConsoleLineKind kind, StringView text) { Print(kind, text); }, [this](StringView module) { Edit(module); },
                  [this](StringView message) { EditLocation(message); }, desc.requestRestart })
    {
        Print(ConsoleLineKind::Info, "Lua console. /help lists the commands, Tab completes names.");
    }

    void ScriptWindow::OnAttach()
    {
        m_querySubscriptions.push_back(RegisterQuery<OpenScriptFile>([this] {
            return OpenScriptFile { [this](const Path& file) {
                m_editor.Open(file);
                m_selectEditor = true;
                m_visible = true;
                m_focusRequested = true;
            } };
        }));
        m_querySubscriptions.push_back(RegisterQuery<PlayControls>([this] { return MakePlayControls(); }));
        m_querySubscriptions.push_back(RegisterQuery<ScriptInspection>([this] { return MakeInspection(); }));
    }

    PlayControls ScriptWindow::MakePlayControls() const
    {
        return PlayControls { m_play.isPlaying && m_play.isPlaying(), m_play.playProject, m_play.playScene, m_play.stop };
    }

    ScriptInspection ScriptWindow::MakeInspection()
    {
        return ScriptInspection {
            .defaults = [this](StringView module) { return m_defaults.Get(module); },
            .forgetDefaults = [this] { m_defaults.Forget(); },
            .instance = [this](NodeId node) {
                const EntityScript* instance = m_entityScripts.Find(node);
                if (!instance)
                    return ScriptInstanceState { };
                if (instance->faulted)
                    return ScriptInstanceState { ScriptInstanceState::Kind::Faulted, instance->error, instance->module };
                return ScriptInstanceState { instance->started ? ScriptInstanceState::Kind::Running : ScriptInstanceState::Kind::Starting };
            },
            .setField = [this](NodeId node, StringView name, const ScriptValue& value) { m_entityScripts.SetField(node, name, value); },
            .endInstance = [this](NodeId node) { m_entityScripts.Destroy({ &node, 1 }); },
            .reloadModule = [this](StringView module) { (void)m_hotReload.Reload(module); },
        };
    }

    void ScriptWindow::Render(ImGuiWindowContext&)
    {
        if (!m_visible)
            return;
        if (std::exchange(m_focusRequested, false))
            ImGui::SetNextWindowFocus();

        ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }
        m_statusBar.Draw();
        DrawTabs();
        ImGui::End();
    }

    void ScriptWindow::DrawTabs()
    {
        if (!ImGui::BeginTabBar("ScriptTabs"))
            return;
        if (ImGui::BeginTabItem("Console", nullptr, m_selectConsole ? ImGuiTabItemFlags_SetSelected : 0)) {
            m_console.Draw();
            ImGui::EndTabItem();
        }
        m_selectConsole = false;
        const String editorLabel = m_editor.UnsavedCount() ? "Editor *###Editor" : "Editor###Editor";
        if (ImGui::BeginTabItem(editorLabel.c_str(), nullptr, m_selectEditor ? ImGuiTabItemFlags_SetSelected : 0)) {
            m_editor.Draw();
            ImGui::EndTabItem();
        }
        m_selectEditor = false;
        if (ImGui::BeginTabItem("Modules")) {
            m_modules.Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("API")) {
            m_api.Draw();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void ScriptWindow::RunCommand(StringView command)
    {
        const size_t space = command.find(' ');
        const StringView name = command.substr(0, space);
        const StringView argument = space == StringView::npos ? StringView() : Trim(command.substr(space + 1));

        if (name == "help")
            Print(ConsoleLineKind::Info, HelpText);
        else if (name == "clear")
            m_console.Clear();
        else if (name == "modules")
            ListModules();
        else if (name == "reload")
            Reload(argument);
        else if (name == "scene")
            m_statusBar.RebuildScene();
        else if (name == "edit")
            Edit(argument.empty() ? StringView(m_mainModule.Name()) : argument);
        else if (name == "restart")
            m_statusBar.RequestRestart();
        else
            Print(ConsoleLineKind::Error, "Unknown command /" + String(name) + ", see /help");
    }

    void ScriptWindow::Reload(StringView module)
    {
        if (module.empty())
            Print(ConsoleLineKind::Info, "Reloaded " + String(std::to_string(m_hotReload.ReloadChanged())) + " changed module(s)");
        else if (auto reloaded = m_hotReload.Reload(module))
            Print(ConsoleLineKind::Info, "Reloaded " + String(module));
        else
            Print(ConsoleLineKind::Error, reloaded.error().message);
    }

    void ScriptWindow::ListModules()
    {
        for (const auto& module : m_loader.LoadedModules())
            Print(module.error.empty() ? ConsoleLineKind::Output : ConsoleLineKind::Error,
                module.name + (module.error.empty() ? "" : "  " + module.error));
    }

    void ScriptWindow::Print(ConsoleLineKind kind, StringView text)
    {
        m_console.Print(kind, text);
        m_console.ScrollToBottom();
    }

    void ScriptWindow::InsertIntoConsole(StringView text)
    {
        m_console.InsertIntoInput(text);
        m_selectConsole = true;
    }

    void ScriptWindow::Edit(StringView module, int line)
    {
        if (m_editor.OpenModule(module, line))
            m_selectEditor = true;
        else
            Print(ConsoleLineKind::Error, "No file for module '" + String(module) + "'");
    }

    void ScriptWindow::EditLocation(StringView message)
    {
        if (m_editor.OpenLocation(message))
            m_selectEditor = true;
        else
            Print(ConsoleLineKind::Error, "The message names no script line");
    }

} // namespace elm::script
