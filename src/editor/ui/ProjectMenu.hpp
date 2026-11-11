#pragma once

#include "core/Std.hpp"

#include <functional>

namespace elm {

    /// The open project as the menus show it and what their items ask the editor for. Given by Editor on every
    /// project switch.
    struct ProjectMenuDesc {
        /// The open project's name, empty without one.
        String name;
        /// Shows the Project Hub: the one place to open, make or switch projects.
        std::function<void()> showHub;
        /// Shows the Settings window on the section seen last (File → Settings, Ctrl+,).
        std::function<void()> showSettings;
        /// Shows the Settings window on the open project's entry: its startup scene and main module.
        std::function<void()> showProjectSettings;
        /// Builds the open project into a game folder, Build/<Name>.
        std::function<void()> buildGame;
    };

    /// The project in the main menu bar: File → Settings (Ctrl+,) and the Project menu, which opens the Project
    /// Hub and, with a project open, its settings and Build Game. Switching projects is the Hub's.
    /// Owner: ImGuiSystem, thread: update (inside the ImGui frame).
    class ProjectMenu {
    public:
        void SetProject(ProjectMenuDesc project);

        /// Inside the File menu.
        void DrawFileItems();
        /// Inside the menu bar.
        void DrawMenu();
        /// Outside of the menu bar, every frame: Ctrl+, works with the menus closed.
        void HandleShortcuts();

    private:
        ProjectMenuDesc m_project;
    };

} // namespace elm
