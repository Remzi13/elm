#include "script/ui/ModulesPanel.hpp"

#include "script/module/MainModule.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/reload/HotReload.hpp"
#include "script/ui/ScriptWindowStyle.hpp"

#include "imgui.h"

namespace elm::script {

    ModulesPanel::ModulesPanel(const ModuleLoader& loader, HotReload& hotReload, const MainModule& mainModule, Actions actions)
        : m_loader(loader)
        , m_hotReload(hotReload)
        , m_mainModule(mainModule)
        , m_actions(std::move(actions))
    {
    }

    void ModulesPanel::Draw()
    {
        const auto& modules = m_loader.LoadedModules();
        if (modules.empty()) {
            ImGui::TextDisabled("No modules loaded");
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("Modules", 4, flags))
            return;
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Reloads", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Reloading and opening change the list: remember the click and act after the loop
        String edit;
        String editError;
        String reload;
        for (const auto& module : modules) {
            ImGui::PushID(module.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(module.name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", module.path.string().c_str());
            if (module.name == m_mainModule.Name()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(main)");
            }

            ImGui::TableNextColumn();
            if (!module.error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, style::ErrorColor);
                ImGui::TextWrapped("%s", module.error.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to open the error line");
                if (ImGui::IsItemClicked())
                    editError = module.error;
            } else if (module.pendingWriteTime != 0) {
                ImGui::TextColored(style::WarningColor, "changed, reloading");
            } else {
                ImGui::TextColored(style::OkColor, "ok");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u", module.reloadCount);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Reload"))
                reload = module.name;
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
                edit = module.name;
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (!edit.empty())
            m_actions.edit(edit);
        if (!editError.empty())
            m_actions.editLocation(editError);
        if (reload.empty())
            return;
        if (auto reloaded = m_hotReload.Reload(reload))
            m_actions.print(ConsoleLineKind::Info, "Reloaded " + reload);
        else
            m_actions.print(ConsoleLineKind::Error, reloaded.error().message);
    }

} // namespace elm::script
