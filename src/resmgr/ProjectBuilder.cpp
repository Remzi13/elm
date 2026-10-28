#include "resmgr/ProjectBuilder.hpp"

#include "resmgr/SerializationError.hpp"

#include "core/Settings.hpp"

#include "project/Project.hpp"

#include <cctype>

namespace elm::resmgr {

    namespace {

        constexpr const char* LogSource = "ProjectBuilder";
        constexpr StringView BuildFolder = "Build";
        constexpr StringView LibraryExtension = ".dll";

        bool IsHidden(const Path& file)
        {
            return file.Filename().string().starts_with(".");
        }

        bool IsLibrary(const Path& file)
        {
            String extension(file.Extension().string().c_str());
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return extension == LibraryExtension;
        }

        auto CopyOne(const Path& from, const Path& to) -> EngineResult<void>
        {
            std::error_code error;
            if (!FileSystem::Copy(from, to, error))
                return UnexpectedSerializationError("Cannot copy " + from.string() + ": " + error.message(), LogSource);
            return { };
        }

        /// Copies a folder's files and folders, hidden ones left out; `skipped` is a name of the top level left out too.
        auto CopyFolder(const Path& from, const Path& to, StringView skipped = { }) -> EngineResult<void>
        {
            std::error_code error;
            FileSystem::CreateDirectories(to, error);
            for (auto it = Directory(from).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path entry = it.CurrentPath();
                const Path name = entry.Filename();
                if (IsHidden(entry) || (!skipped.empty() && name.string() == skipped))
                    continue;
                std::error_code typeError;
                auto copied = it.IsDirectory(typeError) ? CopyFolder(entry, to / name) : CopyOne(entry, to / name);
                if (!copied)
                    return copied;
            }
            if (error)
                return UnexpectedSerializationError("Cannot read " + from.string() + ": " + error.message(), LogSource);
            return { };
        }

        /// Only a folder with the build mark is ours to replace.
        auto ClearOldBuild(const Path& folder) -> EngineResult<void>
        {
            if (!FileSystem::Exists(folder))
                return { };
            if (!FileSystem::Exists(folder / ProjectBuilder::BuildMarkFile))
                return UnexpectedSerializationError("Not a game build, left as it is: " + folder.string(), LogSource);
            std::error_code error;
            if (!FileSystem::RemoveAll(folder, error))
                return UnexpectedSerializationError("Cannot remove the old build " + folder.string() + ": " + error.message(), LogSource);
            return { };
        }

        /// The program and the libraries next to it; the program gets the game's name.
        auto CopyProgram(const BuildDesc& desc, const Path& folder, StringView name) -> EngineResult<void>
        {
            Path program = folder / name;
            program += desc.executable.Extension();
            if (auto copied = CopyOne(desc.executable, program); !copied)
                return copied;
            std::error_code error;
            for (auto it = Directory(desc.executable.Parent()).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                if (!IsLibrary(it.CurrentPath()))
                    continue;
                if (auto copied = CopyOne(it.CurrentPath(), folder / it.CurrentPath().Filename()); !copied)
                    return copied;
            }
            return { };
        }

        /// First of all: a build that fails halfway is still ours to replace.
        auto WriteBuildMark(const Path& folder) -> EngineResult<void>
        {
            std::error_code error;
            FileSystem::CreateDirectories(folder, error);
            auto mark = FileSystem::OpenForWriting(folder / ProjectBuilder::BuildMarkFile);
            mark << "Made by ProjectBuilder: the next build replaces this folder.\n";
            if (error || !mark)
                return UnexpectedSerializationError("Cannot make the build folder " + folder.string(), LogSource);
            return { };
        }

        /// .settings with the folder as the engine root: shaders and the project are found next to the program.
        auto WriteSettings(const Path& folder) -> EngineResult<void>
        {
            Settings settings;
            FileSystem::RegisterSettings(settings);
            return settings.Save(folder / ".settings");
        }

    } // namespace

    auto ProjectBuilder::Build(const project::Project& project, const BuildDesc& desc) -> EngineResult<Path>
    {
        const String& name = project.Name();
        Path folder = project.Root() / BuildFolder / name;
        auto built = ClearOldBuild(folder);
        if (built)
            built = WriteBuildMark(folder);
        if (built)
            built = CopyProgram(desc, folder, name);
        if (built)
            built = CopyFolder(desc.shaders, folder / "shaders");
        Path projectFile = folder / name;
        projectFile += project::Project::FileExtension;
        if (built)
            built = CopyOne(project.File(), projectFile);
        if (built)
            built = CopyFolder(project.Assets(), folder / "assets", project::Project::SourceFolder);
        if (built)
            built = WriteSettings(folder);
        if (!built)
            return UnexpectedSerializationError(built.error());
        return folder;
    }

} // namespace elm::resmgr
