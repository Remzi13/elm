#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Std.hpp"
#include "script/lua/Bind.hpp"

#include <chrono>
#include <functional>

namespace elm::script {

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

    class ScriptModuleManager {
    public:
        using ReloadCallback = std::function<void(StringView name, bool isMain, bool startMain, const String& hookError)>;

        explicit ScriptModuleManager(Path scriptRoot = { });
        ~ScriptModuleManager() = default;

        ScriptModuleManager(const ScriptModuleManager&) = delete;
        ScriptModuleManager& operator=(const ScriptModuleManager&) = delete;

        void Install(lua::State& state);

        [[nodiscard]] const Path& ScriptRoot() const noexcept { return m_scriptRoot; }
        void SetScriptRoot(Path scriptRoot) noexcept { m_scriptRoot = std::move(scriptRoot); }

        [[nodiscard]] const String& MainModuleName() const noexcept { return m_mainModuleName; }
        void SetMainModuleName(String mainModule) { m_mainModuleName = std::move(mainModule); }

        [[nodiscard]] const Vector<LoadedModule>& LoadedModules() const noexcept { return m_loadedModules; }

        [[nodiscard]] auto Require(lua::State& state, StringView name) -> EngineResult<void>;
        [[nodiscard]] Path FindModule(StringView name) const;
        [[nodiscard]] String ModuleNameOf(const Path& file) const;
        [[nodiscard]] String ChunkName(const Path& path) const;

        [[nodiscard]] auto ReloadModule(lua::State& state, StringView name) -> EngineResult<void>;
        size_t ReloadChanged(lua::State& state);
        void PollFileChanges(lua::State& state);

        [[nodiscard]] Vector<String> ListModules() const;
        [[nodiscard]] LoadedModule* FindLoaded(StringView name);

        void SetOnReloaded(ReloadCallback callback) { m_onReloaded = std::move(callback); }

        void Reset();

    private:
        static int SearchModule(lua_State* L);

        Path m_scriptRoot;
        String m_mainModuleName;
        Vector<LoadedModule> m_loadedModules;
        std::chrono::steady_clock::time_point m_nextPoll { };
        ReloadCallback m_onReloaded;
    };

} // namespace elm::script
