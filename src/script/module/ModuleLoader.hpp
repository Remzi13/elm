#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "script/lua/Lua.hpp"

#include <span>

namespace elm::script {

    namespace lua {
        class State;
    }
    class ModulePaths;

    /// A module loaded through require, as the loader and hot reload see it.
    struct LoadedModule {
        String name;
        Path path;
        /// File time of the version that runs.
        int64_t writeTime { 0 };
        /// A newer file time seen by the last poll: reloaded when the next poll still sees it.
        int64_t pendingWriteTime { 0 };
        uint32_t reloadCount { 0 };
        /// Error of the last load or reload, empty while the module is fine.
        String error;
    };

    /// Loads modules for require from the script root and lists the modules loaded since Init.
    /// require("a.b") -> a/b.lua or a/b/init.lua under the root (ModulePaths); package.searchers is
    /// { preload, this loader }: no package.path lookups, no native modules.
    /// Owner: ScriptSystem, thread: update. Before Init the list is empty and Require fails.
    class ModuleLoader {
    public:
        ModuleLoader(lua::State& state, const ModulePaths& paths) noexcept;

        ModuleLoader(const ModuleLoader&) = delete;
        ModuleLoader& operator=(const ModuleLoader&) = delete;
        ModuleLoader(ModuleLoader&&) = delete;
        ModuleLoader& operator=(ModuleLoader&&) = delete;

        /// Installs the searcher into a freshly opened Lua state.
        void Init();
        /// Forgets the loaded modules: their Lua state is going away.
        void Shutdown() noexcept;

        /// require(name) in protected mode, the module value is left on the stack.
        [[nodiscard]] auto Require(StringView name) -> EngineResult<void>;
        /// Reads and compiles a module file and pushes the chunk; nothing is pushed on failure.
        [[nodiscard]] auto LoadChunk(const Path& file) -> EngineResult<void>;

        /// The loaded module of that name, nullptr when it was never required.
        [[nodiscard]] LoadedModule* Find(StringView name) noexcept;
        /// In load order. A module that failed to compile is listed too: it loads once it is fixed.
        [[nodiscard]] const Vector<LoadedModule>& LoadedModules() const noexcept { return m_modules; }
        /// The same entries, for hot reload to keep their file times and errors.
        [[nodiscard]] std::span<LoadedModule> LoadedModules() noexcept { return m_modules; }
        [[nodiscard]] const ModulePaths& Paths() const noexcept { return m_paths; }

    private:
        /// The entry of a module about to be compiled, with the file time of this version.
        LoadedModule& Track(StringView name, const Path& file);
        static int Search(lua_State* L);

        lua::State& m_state;
        const ModulePaths& m_paths;
        Vector<LoadedModule> m_modules;
    };

} // namespace elm::script
