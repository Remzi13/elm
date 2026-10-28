#pragma once

#include "project/AssetPath.hpp"
#include "project/AssetType.hpp"

namespace elm::project {

    /// A file or folder of the assets folder, as the Project window shows it.
    struct AssetEntry {
        /// Empty for the assets folder itself.
        AssetPath path;
        String name;
        bool folder { false };
        /// Files only.
        AssetType type { AssetType::Other };
        /// A folder's entries: folders first, then files, each by name.
        Vector<AssetEntry> children;
    };

    /// The files of a project's assets folder (docs/project/Проекты.md). Read by Refresh, not watched;
    /// folders starting with '.' (.meta) are left out. Owner: the window that shows it, thread: update.
    class ProjectAssets {
    public:
        explicit ProjectAssets(Path assetsFolder);

        /// Reads the folder again.
        void Refresh();

        [[nodiscard]] const Path& Folder() const noexcept { return m_folder; }
        [[nodiscard]] const AssetEntry& Root() const noexcept { return m_root; }
        /// Every file of a type, in the order of the tree.
        [[nodiscard]] Vector<AssetPath> FilesOf(AssetType type) const;
        /// Lua module names of the scripts folder: scripts/npc/guard.lua -> "npc.guard", scripts/pkg/init.lua -> "pkg".
        [[nodiscard]] Vector<String> ScriptModules() const;

    private:
        Path m_folder;
        AssetEntry m_root;
    };

} // namespace elm::project
