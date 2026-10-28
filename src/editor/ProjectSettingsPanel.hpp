#pragma once

#include "core/Error.hpp"

#include "project/Project.hpp"
#include "project/ProjectAssets.hpp"

#include <functional>

namespace elm::editor {

    /// Sets the entry of the open project and writes its file; the error says why it did not.
    using SetProjectEntry = std::function<EngineResult<void>(project::ProjectEntry)>;

    /// The Settings tab of the Project window (docs/project/Проекты.md#Окно Project): the project's entry,
    /// the startup scene and the main module, picked from the project's scenes and scripts. Save writes the
    /// project file; an entry with neither is not saved. An entry changed elsewhere (Set as Startup Scene)
    /// shows up here unless the tab has edits of its own.
    /// Owner: ProjectWindow, thread: update (inside the ImGui frame).
    class ProjectSettingsPanel {
    public:
        /// assets: the Project window's list of the project's files, read for the scenes and scripts.
        ProjectSettingsPanel(const project::Project& project, SetProjectEntry setEntry, const project::ProjectAssets& assets);

        void Draw();

    private:
        void TakeOutsideChanges();
        void DrawStartupScene();
        void DrawMainModule();
        void DrawButtons();
        [[nodiscard]] bool IsChanged() const;

        const project::Project& m_project;
        SetProjectEntry m_setEntry;
        const project::ProjectAssets& m_assets;
        /// The entry being edited, the project's own until Save.
        project::ProjectEntry m_entry;
        /// The project's entry when the tab last looked: a change of it came from elsewhere.
        project::ProjectEntry m_seenEntry;
        String m_status;
    };

} // namespace elm::editor
