#include "project/Project.hpp"

#include <utility>

namespace elm::project {

    Project::Project(Path file, ProjectDesc desc)
        : m_file(std::move(file))
        , m_desc(std::move(desc))
    {
    }

    Path Project::FindFile(const Path& folderOrFile)
    {
        if (folderOrFile.Extension().string() == FileExtension)
            return folderOrFile;
        // "Games/CoinRush/" has an empty file name: the folder name is the one of its parent
        const Path folder = folderOrFile.HasFilename() ? folderOrFile : folderOrFile.Parent();
        Path file = folder / folder.Filename();
        file += FileExtension;
        return file;
    }

    Path Project::Root() const
    {
        return m_file.Parent();
    }

    Path Project::Assets() const
    {
        return Root() / "assets";
    }

    Path Project::Models() const
    {
        return Assets() / ModelsFolder;
    }

    Path Project::Scenes() const
    {
        return Assets() / ScenesFolder;
    }

    Path Project::Scripts() const
    {
        return Assets() / ScriptsFolder;
    }

    Path Project::Resolve(const AssetPath& asset) const
    {
        return asset.In(Assets());
    }

    std::optional<AssetPath> Project::AssetPathOf(const Path& file) const
    {
        return AssetPath::Of(file, Assets());
    }

} // namespace elm::project
