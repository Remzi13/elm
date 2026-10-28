#include "graphics/ui/ProjectMenu.hpp"

#include "imgui.h"

#include <utility>

namespace elm {

namespace {

    constexpr const char* ConfirmationPopup = "Close Project";

} // namespace

void ProjectMenu::SetProject(ProjectMenuDesc project)
{
    m_project = std::move(project);
}

void ProjectMenu::DrawMenu()
{
    if (!ImGui::BeginMenu("Project"))
        return;
    const bool hasProject = !m_project.file.Empty();
    ImGui::TextDisabled("%s", hasProject ? m_project.name.c_str() : "No project");
    ImGui::Separator();
    if (ImGui::MenuItem("Project Hub...") && m_project.showHub)
        m_project.showHub();
    if (hasProject && ImGui::MenuItem("Open Project..."))
        m_openFileDialog = true;
    DrawRecent();
    if (hasProject && ImGui::MenuItem("Reload Project"))
        RequestOpen(m_project.file);
    ImGui::Separator();
    if (hasProject && ImGui::MenuItem("Run Standalone", "Ctrl+F5"))
        RunStandalone();
    if (hasProject && ImGui::MenuItem("Build Game") && m_project.buildGame)
        m_project.buildGame();
    ImGui::EndMenu();
}

void ProjectMenu::RunStandalone()
{
    if (!m_project.file.Empty() && m_project.runStandalone)
        m_project.runStandalone();
}

void ProjectMenu::DrawRecent()
{
    if (!ImGui::BeginMenu("Recent", !m_project.recent.empty()))
        return;
    for (const Path& file : m_project.recent) {
        if (ImGui::MenuItem(file.string().c_str()))
            RequestOpen(file);
    }
    ImGui::EndMenu();
}

void ProjectMenu::RequestOpen(const Path& file)
{
    if (m_project.file.Empty()) {
        if (m_project.open)
            m_project.open(file);
        return;
    }
    m_pendingFile = file;
    m_openConfirmation = true;
}

void ProjectMenu::DrawPopups()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F5, false))
        RunStandalone();
    // Projects usually lie next to each other: the dialog starts in the folder of the open one
    if (std::exchange(m_openFileDialog, false))
        m_fileDialog.Open("Open Project", m_project.fileExtension, m_project.file.Parent().Parent());
    if (const auto file = m_fileDialog.Draw())
        RequestOpen(*file);
    DrawConfirmation();
}

void ProjectMenu::DrawConfirmation()
{
    if (std::exchange(m_openConfirmation, false))
        ImGui::OpenPopup(ConfirmationPopup);
    if (!ImGui::BeginPopupModal(ConfirmationPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const bool reload = m_pendingFile == m_project.file;
    ImGui::Text("%s project \"%s\"?", reload ? "Reload the" : "Close the", m_project.name.c_str());
    ImGui::TextUnformatted("Unsaved changes of the scene, models and scripts are lost.");
    if (ImGui::Button(reload ? "Reload" : "Open")) {
        if (m_project.open && m_pendingFile)
            m_project.open(*m_pendingFile);
        m_pendingFile.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_pendingFile.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace elm
