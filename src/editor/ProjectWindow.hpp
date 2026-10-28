#pragma once

#include "core/Error.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

#include "editor/ProjectSettingsPanel.hpp"

#include "project/Project.hpp"
#include "project/ProjectAssets.hpp"

#include <array>
#include <functional>
#include <optional>

namespace elm::editor {

    /// The open project (docs/project/Проекты.md#Окно Project). The Assets tab: a tree of the assets folder,
    /// search by name and a filter by AssetType. A double click opens a scene in Scene Hierarchy, a model in
    /// the Model Editor and a script in the Scripts window; a model is dragged into Scene Hierarchy. The
    /// context menu makes folders and sets the startup scene. The Settings tab: the project's entry
    /// (ProjectSettingsPanel).
    /// Owner: ImGuiSystem (a project window: it lives as long as the project is open), thread: update.
    class ProjectWindow final : public IImGuiWindow {
    public:
        ProjectWindow(Settings& settings, const project::Project& project, SetProjectEntry setEntry);

        [[nodiscard]] StringView GetName() const override { return "Project"; }
        void Render(ImGuiWindowContext& context) override;

    private:
        void DrawAssets();
        void DrawToolbar();
        void DrawFolder(const project::AssetEntry& folder);
        void DrawFile(const project::AssetEntry& file, StringView label);
        void DrawFound(const project::AssetEntry& folder);
        void DrawFileMenu(const project::AssetEntry& file);
        void DrawNewFolderPopup();
        void Open(const project::AssetEntry& file);
        void SetStartupScene(const project::AssetPath& scene);
        [[nodiscard]] bool IsFiltering() const;
        [[nodiscard]] bool Matches(const project::AssetEntry& file) const;

        const project::Project& m_project;
        SetProjectEntry m_setEntry;
        project::ProjectAssets m_assets;
        /// After m_assets: it reads them.
        ProjectSettingsPanel m_settingsPanel;
        std::array<char, 128> m_search { };
        std::optional<project::AssetType> m_typeFilter;
        /// The folder New Folder makes a folder in; empty: the assets folder.
        project::AssetPath m_newFolderParent;
        std::array<char, 64> m_newFolderName { };
        bool m_openNewFolder { false };
        String m_status;
    };

} // namespace elm::editor
