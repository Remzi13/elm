#include "editor/ProjectHubWindow.hpp"

#include "project/Project.hpp"

#include "imgui.h"

#include <cstdio>
#include <utility>

namespace elm::editor {

    ProjectHubWindow::ProjectHubWindow(Settings& settings, ProjectHubDesc desc)
        : IImGuiWindow(settings)
        , m_desc(std::move(desc))
    {
        std::snprintf(m_location.data(), m_location.size(), "%s", m_desc.defaultLocation.string().c_str());
        m_visible = false;
    }

    void ProjectHubWindow::Render(ImGuiWindowContext&)
    {
        if (!m_visible)
            return;
        ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }
        DrawRecent();
        DrawOpen();
        DrawCreate();
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::End();
    }

    void ProjectHubWindow::DrawRecent()
    {
        ImGui::SeparatorText("Recent projects");
        const Vector<Path> recent = m_desc.recent ? m_desc.recent() : Vector<Path> { };
        if (recent.empty())
            ImGui::TextDisabled("None yet");
        for (const Path& file : recent) {
            const String label(file.Stem().string().c_str());
            if (ImGui::Selectable((label + "##" + String(file.string().c_str())).c_str()) && m_desc.open)
                m_desc.open(file);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", file.string().c_str());
        }
    }

    void ProjectHubWindow::DrawOpen()
    {
        ImGui::SeparatorText("Open");
        if (ImGui::Button("Open Project..."))
            m_openDialog.Open("Open Project", project::Project::FileExtension, m_desc.defaultLocation);
        if (const auto file = m_openDialog.Draw(); file && m_desc.open)
            m_desc.open(*file);
    }

    void ProjectHubWindow::DrawCreate()
    {
        ImGui::SeparatorText("Create");
        ImGui::InputTextWithHint("Name", "MyGame: letters, digits, _ and -", m_name.data(), m_name.size());
        ImGui::InputText("Location", m_location.data(), m_location.size());
        ImGui::TextDisabled("The project folder is <Location>/<Name>, the game's name");
        ImGui::BeginDisabled(m_name[0] == '\0' || !m_desc.create);
        if (ImGui::Button("Create Project")) {
            const auto created = m_desc.create(Path(m_location.data()), StringView(m_name.data()));
            m_status = created ? "Creating " + String(m_name.data()) + "..." : created.error().message;
        }
        ImGui::EndDisabled();
    }

} // namespace elm::editor
