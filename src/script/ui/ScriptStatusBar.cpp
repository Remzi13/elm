#include "script/ui/ScriptStatusBar.hpp"

#include "script/entity/EntityScripts.hpp"
#include "script/lua/State.hpp"
#include "script/module/MainModule.hpp"
#include "script/reload/HotReload.hpp"
#include "script/ui/ScriptWindowStyle.hpp"

#include "core/Settings.hpp"

#include "imgui.h"

namespace elm::script {

    ScriptStatusBar::ScriptStatusBar(Settings& settings, Parts parts, Actions actions)
        : m_settings(settings)
        , m_parts(parts)
        , m_actions(std::move(actions))
    {
    }

    void ScriptStatusBar::Draw()
    {
        if (!m_parts.state.IsOpen()) {
            ImGui::TextColored(style::ErrorColor, "Scripts are not initialized");
            return;
        }
        DrawState();
        DrawButtons();
        DrawMainError();
        ImGui::Separator();
    }

    void ScriptStatusBar::DrawState()
    {
        ImGui::TextDisabled("%s", LUA_RELEASE);
        ImGui::SameLine();
        const String& mainName = m_parts.mainModule.Name();
        switch (m_parts.mainModule.GetState()) {
        case MainState::Running:
            ImGui::TextColored(style::OkColor, "main: %s (running)", mainName.c_str());
            break;
        case MainState::Faulted:
            ImGui::TextColored(style::ErrorColor, "main: %s (stopped)", mainName.c_str());
            break;
        case MainState::Idle:
            ImGui::TextColored(style::InfoColor, "main: %s (not running)", mainName.empty() ? "-" : mainName.c_str());
            break;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %.1f KB", static_cast<double>(m_parts.state.MemoryUsage()) / 1024.0);

        size_t running = 0;
        size_t stopped = 0;
        const EntityScript* firstStopped = nullptr;
        for (const auto& script : m_parts.entityScripts.All()) {
            if (script.dead)
                continue;
            ++running;
            if (script.faulted && !stopped++)
                firstStopped = &script;
        }
        ImGui::SameLine();
        if (!firstStopped) {
            ImGui::TextDisabled("| entity scripts: %zu", running);
            return;
        }
        ImGui::TextColored(style::ErrorColor, "| entity scripts: %zu, %zu stopped", running, stopped);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: %s\nA module reload starts them again", firstStopped->module.c_str(), firstStopped->error.c_str());
    }

    void ScriptStatusBar::DrawButtons()
    {
        bool hotReload = m_settings.Get<bool>(Settings::Category::Script, HotReload::EnabledSetting, true);
        if (ImGui::Checkbox("Hot reload", &hotReload))
            m_settings.Set(Settings::Category::Script, HotReload::EnabledSetting, hotReload);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reload a module as soon as its file is saved");
        ImGui::SameLine();
        if (ImGui::Button("Reload changed")) {
            const size_t reloaded = m_parts.hotReload.ReloadChanged();
            m_actions.print(ConsoleLineKind::Info, "Reloaded " + String(std::to_string(reloaded)) + " changed module(s)");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_parts.mainModule.GetState() == MainState::Idle);
        if (ImGui::Button("Rebuild scene"))
            RebuildScene();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Calls main.build_scene()");
        ImGui::SameLine();
        if (ImGui::Button("Edit main"))
            m_actions.edit(m_parts.mainModule.Name());
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
            RequestRestart();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New Lua state: all modules and their state are loaded again");
    }

    void ScriptStatusBar::DrawMainError()
    {
        if (m_parts.mainModule.GetState() != MainState::Faulted)
            return;
        ImGui::PushStyleColor(ImGuiCol_Text, style::ErrorColor);
        ImGui::TextWrapped("%s", m_parts.mainModule.Error().c_str());
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Reload main module")) {
            if (auto reloaded = m_parts.hotReload.Reload(m_parts.mainModule.Name()); !reloaded)
                m_actions.print(ConsoleLineKind::Error, reloaded.error().message);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Show the error line"))
            m_actions.editLocation(m_parts.mainModule.Error());
    }

    void ScriptStatusBar::RebuildScene()
    {
        if (auto built = m_parts.mainModule.CallFunction(MainModule::BuildSceneFunction); !built)
            m_actions.print(ConsoleLineKind::Error, built.error().message);
        else if (!*built)
            m_actions.print(ConsoleLineKind::Error, "The main module has no build_scene() function");
        else
            m_actions.print(ConsoleLineKind::Info, "Scene rebuilt by " + m_parts.mainModule.Name() + ".build_scene()");
    }

    void ScriptStatusBar::RequestRestart()
    {
        m_actions.requestRestart();
        m_actions.print(ConsoleLineKind::Info, "Scripts restart on the next frame");
    }

} // namespace elm::script
