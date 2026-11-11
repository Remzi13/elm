#pragma once

#include "editor/ui/FileDialog.hpp"

#include "project/AssetPath.hpp"

#include <optional>

namespace elm {

    class Scene;
    class SceneDocument;
    class SceneEdits;
    struct Selection;

    namespace project {
        class Project;
    }

    /// What the scene's menus work on while a project is open; nullptrs without one. It all lives until the next
    /// SetDocument.
    struct SceneMenuDeps {
        SceneDocument* document { nullptr };
        SceneEdits* edits { nullptr };
        /// Duplicate and Delete of the Edit menu: the selected node.
        Scene* scene { nullptr };
        Selection* selection { nullptr };
        /// The scene list at the file name: the project's scenes and its startup scene.
        const project::Project* project { nullptr };
    };

    /// The scene in the main menu bar: its items of the File menu (New, Open, Save, Save As, glTF), the Edit menu
    /// (Undo, Redo, Duplicate, Delete of the selected node), and at the right edge the scene's last status and the
    /// name of its file, which opens the list of the project's scenes. Asks before unsaved changes go.
    /// Owner: ImGuiSystem, thread: update (inside the ImGui frame).
    class SceneMenu {
    public:
        /// Empty without a project: the items are off.
        void SetDocument(const SceneMenuDeps& deps);

        /// Inside the File menu.
        void DrawFileItems();
        /// Inside the menu bar.
        void DrawEditMenu();
        /// Inside the menu bar, at its right edge: the status as far as it fits, then the file name. The name is a
        /// menu of the project's scenes; choosing one opens it like RequestOpen.
        void DrawFileName();
        /// Outside of the menu bar and before the windows of the frame: this is where the scene gets replaced.
        void DrawPopups();

        /// Empties the scene in the next DrawPopups, after asking when it has unsaved changes.
        void RequestNew();
        /// Replaces the scene with a scene file the same way. From anywhere in the frame.
        void RequestOpen(const Path& file);

    private:
        /// What replaces the scene.
        struct Replacement {
            /// Empty: a new scene.
            Path file;
        };

        void DrawStatus(float right);
        void DrawSceneList();
        void DrawDialogs();
        void Apply(const Replacement& replacement);
        void DrawConfirmation();

        SceneDocument* m_document { nullptr };
        SceneEdits* m_edits { nullptr };
        Scene* m_scene { nullptr };
        Selection* m_selection { nullptr };
        const project::Project* m_project { nullptr };
        /// The project's scene files, read again each time the list opens: Save As may have added one.
        Vector<project::AssetPath> m_sceneFiles;
        ui::FileDialog m_openDialog;
        ui::FileDialog m_saveDialog;
        ui::FileDialog m_importDialog;
        ui::FileDialog m_exportDialog;
        std::optional<Replacement> m_requested;
        /// Waits for the answer to the confirmation.
        std::optional<Replacement> m_pending;
    };

} // namespace elm
