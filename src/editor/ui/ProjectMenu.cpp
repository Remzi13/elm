#include "editor/ui/ProjectMenu.hpp"

#include "imgui.h"

#include <utility>

namespace elm {

    void ProjectMenu::SetProject(ProjectMenuDesc project)
    {
        m_project = std::move(project);
    }

    void ProjectMenu::DrawFileItems()
    {
        if (ImGui::MenuItem("Settings...", "Ctrl+,") && m_project.showSettings)
            m_project.showSettings();
    }

    void ProjectMenu::DrawMenu()
    {
        if (!ImGui::BeginMenu("Project"))
            return;
        if (ImGui::MenuItem("Open Project...") && m_project.showHub)
            m_project.showHub();
        ImGui::SetItemTooltip("The Project Hub: recent projects, open, create");
        ImGui::Separator();
        const bool hasProject = !m_project.name.empty();
        if (ImGui::MenuItem("Project Settings...", nullptr, false, hasProject) && m_project.showProjectSettings)
            m_project.showProjectSettings();
        if (ImGui::MenuItem("Build Game", nullptr, false, hasProject) && m_project.buildGame)
            m_project.buildGame();
        ImGui::SetItemTooltip("Builds the saved files into Build/%s", m_project.name.c_str());
        ImGui::EndMenu();
    }

    void ProjectMenu::HandleShortcuts()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false) && m_project.showSettings)
            m_project.showSettings();
    }

} // namespace elm
