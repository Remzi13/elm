#pragma once

#include "graphics/ui/FileDialog.hpp"

#include <functional>
#include <optional>

namespace elm {

/// The open project as the Project menu shows it, and how the menu asks the engine to open one.
/// Given by EngineApp on every project switch.
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
    /// Runs the open project in a player process of its own, from its files (Ctrl+F5).
    std::function<void()> runStandalone;
    /// Builds the open project into a game folder, Build/<Name>.
    std::function<void()> buildGame;
};

/// The Project menu of the main menu bar: opens another project (a recent one, a file) or reloads the open
/// one, after a confirmation, since unsaved changes are lost; shows the Project Hub. Without a project only
/// the hub and the recent projects are offered. Owner: ImGuiSystem, thread: update (inside the ImGui frame).
class ProjectMenu {
public:
    void SetProject(ProjectMenuDesc project);

    /// Inside the menu bar.
    void DrawMenu();
    /// Outside of the menu bar: popups opened from a menu would live in the menu's ID scope.
    void DrawPopups();

private:
    void DrawRecent();
    void RunStandalone();
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
