#include "editor/ProjectHubWindow.hpp"

#include "project/Project.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace elm::editor {

    namespace {

        /// Marks the open project in the recent ones (U+2713 of the icon font, EditorStyle).
        constexpr const char* OpenMark = "\xE2\x9C\x93 ";
        constexpr const char* NoMark = "   ";
        constexpr const char* ReloadButton = "Reload";
        /// Where the folder of a recent project starts, right of its name.
        constexpr float PathColumn = 180.0f;

    } // namespace

    ProjectHubWindow::ProjectHubWindow(Settings& settings, ProjectHubDesc desc)
        : IImGuiWindow(settings)
        , m_desc(std::move(desc))
    {
        std::snprintf(m_location.data(), m_location.size(), "%s", m_desc.defaultLocation.string().c_str());
        m_visible = false;
    }

    void ProjectHubWindow::Render(render::FrameWriter&)
    {
        if (!m_visible)
            return;
        ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(Name, &m_visible)) {
            ImGui::End();
            return;
        }
        DrawRecent();
        DrawOpen();
        DrawCreate();
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        DrawConfirmation();
        ImGui::End();
    }

    void ProjectHubWindow::DrawRecent()
    {
        ImGui::SeparatorText("Recent projects");
        const Vector<Path> recent = m_desc.recent ? m_desc.recent() : Vector<Path> {};
        if (recent.empty())
            ImGui::TextDisabled("None yet");
        const Path openProject = m_desc.openProject ? m_desc.openProject() : Path {};
        for (const Path& file : recent) {
            const bool isOpen = !openProject.Empty() && file == openProject;
            const String path(file.string().c_str());
            const String label = String(isOpen ? OpenMark : NoMark) + file.Stem().string().c_str() + "###" + path;
            if (ImGui::Selectable(label.c_str(), isOpen))
                Confirm(isOpen ? ReloadButton : "Open", [this, file] { m_desc.open(file); });
            if (isOpen)
                ImGui::SetItemTooltip("Open now: a click reloads it");
            ImGui::SameLine(PathColumn);
            ImGui::TextDisabled("%s", file.Parent().string().c_str());
        }
    }

    void ProjectHubWindow::DrawOpen()
    {
        ImGui::SeparatorText("Open");
        if (ImGui::Button("Open Project..."))
            m_openDialog.Open("Open Project", project::Project::FileExtension, m_desc.defaultLocation);
        if (const auto file = m_openDialog.Draw())
            Confirm("Open", [this, file = *file] { m_desc.open(file); });
    }

    void ProjectHubWindow::DrawCreate()
    {
        ImGui::SeparatorText("Create");
        ImGui::InputTextWithHint("Name", "MyGame: letters, digits, _ and -", m_name.data(), m_name.size());
        ImGui::InputText("Location", m_location.data(), m_location.size());
        ImGui::TextDisabled("The project folder is <Location>/<Name>, the game's name");
        ImGui::BeginDisabled(m_name[0] == '\0' || !m_desc.create);
        if (ImGui::Button("Create Project")) {
            Confirm("Create", [this] {
                const auto created = m_desc.create(Path(m_location.data()), StringView(m_name.data()));
                m_status = created ? "Creating " + String(m_name.data()) + "..." : created.error().message;
            });
        }
        ImGui::EndDisabled();
    }

    void ProjectHubWindow::Confirm(const char* button, std::function<void()> action)
    {
        if (!m_desc.openProject || m_desc.openProject().Empty()) {
            action();
            return;
        }
        m_confirmed = std::move(action);
        m_confirmButton = button;
        m_openConfirmation = true;
    }

    void ProjectHubWindow::DrawConfirmation()
    {
        if (std::exchange(m_openConfirmation, false))
            ImGui::OpenPopup(ConfirmationPopup);
        if (!ImGui::BeginPopupModal(ConfirmationPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        const bool reload = std::strcmp(m_confirmButton, ReloadButton) == 0;
        const String name(m_desc.openProject().Stem().string().c_str());
        ImGui::Text("%s project \"%s\"?", reload ? "Reload the" : "Close the", name.c_str());
        ImGui::TextUnformatted("Unsaved changes of the scene, models and scripts are lost.");
        if (ImGui::Button(m_confirmButton)) {
            if (m_confirmed)
                std::exchange(m_confirmed, {})();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_confirmed = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

} // namespace elm::editor
