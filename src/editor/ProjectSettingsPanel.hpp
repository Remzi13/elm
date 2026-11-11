#pragma once

#include "core/Error.hpp"

#include "project/Project.hpp"
#include "project/ProjectAssets.hpp"

#include <functional>

namespace elm::editor {

    /// Sets the entry of the open project and writes its file; the error says why it did not.
    using SetProjectEntry = std::function<EngineResult<void>(project::ProjectEntry)>;

    /// The Project section of the Settings window (docs/project/Проекты.md#Точка входа и запуск): the project's
    /// entry, the startup scene and the main module, picked from the project's scenes and scripts. A choice is
    /// written to the project file at once; an entry with neither is refused. It always shows the project's own
    /// entry, so a change made elsewhere (Set as Startup Scene) shows up too.
    /// Owner: SettingsWindow, while a project is open. Thread: update (inside the ImGui frame).
    class ProjectSettingsPanel {
    public:
        ProjectSettingsPanel(const project::Project& project, SetProjectEntry setEntry);

        void Draw();

    private:
        void DrawStartupScene();
        void DrawMainModule();
        void Apply(project::ProjectEntry entry);

        const project::Project& m_project;
        SetProjectEntry m_setEntry;
        /// Read again when a list opens: files added since show up.
        project::ProjectAssets m_assets;
        String m_status;
    };

} // namespace elm::editor
