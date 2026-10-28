#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

namespace elm::project {
    class Project;
}

namespace elm::resmgr {

    /// The engine's own files a game is built with.
    struct BuildDesc {
        /// The program the game runs in: copied as <Name> (with its extension), the libraries next to it too.
        Path executable;
        /// The engine's shader folder.
        Path shaders;
    };

    /// Builds a project into a game folder (docs/project/Проекты.md#Сборка): <project>/Build/<Name>/ with the
    /// program as <Name>.exe and the libraries next to it, shaders/, a .settings that makes the folder the
    /// engine root, <Name>.elmproject and assets/ without source/ and hidden files (.meta, .luarc.json).
    /// The program finds the project named like itself next to it and plays it. Thread: any.
    class ProjectBuilder {
    public:
        ProjectBuilder() = delete;

        /// Marks a folder ProjectBuilder made: only such a folder is replaced by the next build.
        static constexpr StringView BuildMarkFile = ".elmbuild";

        /// Returns the game folder. A Build/<Name> folder that is not a build of ours is an error, nothing is removed.
        [[nodiscard]] static auto Build(const project::Project& project, const BuildDesc& desc) -> EngineResult<Path>;
    };

} // namespace elm::resmgr
