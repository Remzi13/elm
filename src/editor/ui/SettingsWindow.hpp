#pragma once

#include "editor/ProjectSettingsPanel.hpp"
#include "editor/ui/IImGuiWindow.hpp"

#include <cstdint>
#include <optional>

namespace elm {

    /// A section of the Settings window.
    enum class SettingsSection : uint8_t {
        Project,
        Editor,
        Rendering
    };

    /// The settings of the editor and of the open project, each in one place (docs/editor/Редактор.md#Настройки):
    /// a list of sections and the settings of the chosen one. Every setting is drawn by its own key, so the keys
    /// the editor keeps for itself (the root path, the recent projects) are not shown, and a new setting is added
    /// here by hand. A change is written to the settings file at once; the Project section writes the project
    /// file. Owner: ImGuiSystem (outlives projects), thread: update (inside the ImGui frame).
    class SettingsWindow final : public IImGuiWindow {
    public:
        /// file: where a change is written, the program's .settings.
        SettingsWindow(Settings& settings, Path file);

        [[nodiscard]] StringView GetName() const override { return "Settings"; }
        void Render(render::FrameWriter& frame) override;

        /// Shows the window in front of the others on the section.
        void Show(SettingsSection section);
        /// The open project for the Project section; it lives until ClearProject.
        void SetProject(const project::Project& project, editor::SetProjectEntry setEntry);
        void ClearProject();

    private:
        void DrawSectionList();
        /// True when a setting changed and the file should be written.
        [[nodiscard]] bool DrawEditorSection();
        [[nodiscard]] bool DrawRenderingSection();
        void WriteFile();

        Path m_file;
        SettingsSection m_section { SettingsSection::Editor };
        /// Show waits for the next frame.
        bool m_bringToFront { false };
        std::optional<editor::ProjectSettingsPanel> m_project;
        /// Why the file was not written.
        String m_error;
    };

} // namespace elm
