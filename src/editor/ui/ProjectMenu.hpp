#pragma once

#include "editor/ui/FileDialog.hpp"

#include <functional>
#include <optional>

namespace elm {

    /// The open project as the Project menu shows it, and how the menu asks the engine to open one.
    /// Given by Editor on every project switch.
    struct ProjectMenuDesc {
        String name;
        /// The project file: opening it again reloads the project.
        Path file;
        /// Extension of project files, for the Open dialog.
        String fileExtension;
        /// A request: the engine switches projects at the start of the next update, outside of any window.
        std::function<void(const Path& file)> open;
        /// Project files opened before, the last one first.
        Vector<Path> recent;
        /// Shows the Project Hub: create a project, open one.
        std::function<void()> showHub;
        /// Shows the entry of the open project: its startup scene and main module.
        std::function<void()> showSettings;
        /// Builds the open project into a game folder, Build/<Name>.
        std::function<void()> buildGame;
    };

    /// The project in the main menu bar. Its items of the File menu open another project (a new one in the Project
    /// Hub, a file, a recent one) or reload the open one, after a confirmation, since unsaved changes are lost. The
    /// Project menu is the open project: its settings and Build Game; off without a project.
    /// Owner: ImGuiSystem, thread: update (inside the ImGui frame).
    class ProjectMenu {
    public:
        void SetProject(ProjectMenuDesc project);

        /// Inside the File menu.
        void DrawFileItems();
        /// Inside the menu bar.
        void DrawMenu();
        /// Outside of the menu bar: popups opened from a menu would live in the menu's ID scope.
        void DrawPopups();

    private:
        void DrawRecent();
        /// Asks before closing an open project, opens right away without one.
        void RequestOpen(const Path& file);
        void DrawConfirmation();

        ProjectMenuDesc m_project;
        ui::FileDialog m_fileDialog;
        bool m_openFileDialog { false };
        /// The project file waiting for the confirmation; the open project's own file to reload it.
        std::optional<Path> m_pendingFile;
        bool m_openConfirmation { false };
    };

} // namespace elm
