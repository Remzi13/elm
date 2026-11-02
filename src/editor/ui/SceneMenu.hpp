#pragma once

#include "editor/ui/FileDialog.hpp"

#include <optional>

namespace elm {

class SceneDocument;
class SceneEdits;

/// The Scene and Edit menus of the main menu bar and the open scene's name beside them: New, Open, Save, Save As,
/// glTF, Undo, Redo. Asks before unsaved changes go. Owner: ImGuiSystem, thread: update (inside the ImGui frame).
class SceneMenu {
public:
    /// Nullptrs without a project: the menus are off. Both live until the next call.
    void SetDocument(SceneDocument* document, SceneEdits* edits);

    /// Inside the menu bar.
    void DrawMenus();
    /// Inside the menu bar, at its right edge.
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

    void DrawSceneMenu();
    void DrawEditMenu();
    void DrawDialogs();
    void Apply(const Replacement& replacement);
    void DrawConfirmation();

    SceneDocument* m_document { nullptr };
    SceneEdits* m_edits { nullptr };
    ui::FileDialog m_openDialog;
    ui::FileDialog m_saveDialog;
    ui::FileDialog m_importDialog;
    ui::FileDialog m_exportDialog;
    std::optional<Replacement> m_requested;
    /// Waits for the answer to the confirmation.
    std::optional<Replacement> m_pending;
};

} // namespace elm
