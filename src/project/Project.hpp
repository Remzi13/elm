#pragma once

#include "core/FileSystem.hpp"

#include "project/AssetPath.hpp"
#include "project/ProjectDesc.hpp"

#include <optional>
#include <utility>

namespace elm::project {

    /// An open game project: its file, description and asset folders (docs/project/Проекты.md).
    /// A value: the editor replaces it when another project is opened. Owner: EngineApp, thread: update.
    class Project {
    public:
        static constexpr StringView FileExtension = ".elmproject";
        /// Folders of the assets folder, as asset paths start with them.
        static constexpr StringView ModelsFolder = "models";
        static constexpr StringView ScenesFolder = "scenes";
        static constexpr StringView ScriptsFolder = "scripts";
        /// Originals of imported files (glTF with its buffers and images), kept for a later import.
        static constexpr StringView SourceFolder = "source";

        /// file: the absolute path of the <Name>.elmproject file.
        Project(Path file, ProjectDesc desc);

        /// The project file a path names: a .elmproject file as is, a folder as <folder>/<folder>.elmproject.
        [[nodiscard]] static Path FindFile(const Path& folderOrFile);

        [[nodiscard]] const Path& File() const noexcept { return m_file; }
        [[nodiscard]] const ProjectDesc& Desc() const noexcept { return m_desc; }
        [[nodiscard]] const String& Name() const noexcept { return m_desc.name; }
        /// The entry the Project window chose; Serializer::SaveProject writes it.
        void SetEntry(ProjectEntry entry) { m_desc.entry = std::move(entry); }

        /// The folder of the project file.
        [[nodiscard]] Path Root() const;
        /// The "assets" folder with everything the game loads.
        [[nodiscard]] Path Assets() const;
        [[nodiscard]] Path Models() const;
        [[nodiscard]] Path Scenes() const;
        [[nodiscard]] Path Scripts() const;

        /// The file of an asset path, under Assets().
        [[nodiscard]] Path Resolve(const AssetPath& asset) const;
        /// The asset path of a file under Assets(); nothing for a file outside of it.
        [[nodiscard]] std::optional<AssetPath> AssetPathOf(const Path& file) const;

    private:
        Path m_file;
        ProjectDesc m_desc;
    };

} // namespace elm::project
