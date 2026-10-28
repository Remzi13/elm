#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

namespace elm::resmgr {

    /// A new project on disk (docs/project/Проекты.md#Проект на диске): <location>/<name>/<name>.elmproject
    /// with main as its main module, assets/scenes, assets/models and assets/scripts with main.lua and
    /// the LuaLS setup (.luarc.json, .meta/elm.lua).
    class ProjectTemplate {
    public:
        ProjectTemplate() = delete;

        /// A name is a folder name on every platform: letters, digits, '_' and '-'.
        [[nodiscard]] static bool IsValidName(StringView name);
        /// Writes the project; luaApi is the LuaLS description of the engine's Lua API. A folder that
        /// exists already is an error: nothing is overwritten. Returns the project file.
        [[nodiscard]] static auto Create(const Path& location, StringView name, StringView luaApi) -> EngineResult<Path>;
    };

} // namespace elm::resmgr
