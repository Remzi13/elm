#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include <chrono>
#include <functional>

namespace elm::script {

    namespace lua {
        class State;
    }
    class ModuleLoader;

    /// Hot reload of the loaded modules. A module is run again and its new table is copied into the
    /// old one, so everything holding the module (require results, the main module, script classes)
    /// sees the new functions; the module's own upvalues that held the new table (local M = {}) are
    /// pointed at the old one. Fields absent from the new version are removed: state that must survive
    /// lives in elm.script.persist() or is taken over in on_reload(previous) from the old fields.
    /// A module that fails to load keeps its previous version running.
    /// Owner: ScriptSystem, thread: update. Who reacts to a reload is up to the owner (Init's callback).
    class HotReload {
    public:
        /// Setting Script/HotReload: whether the owner polls the module files.
        static constexpr StringView EnabledSetting = "HotReload";

        /// Called after a module reloaded: the new code is installed (its on_reload may have failed).
        using ModuleReloaded = std::function<void(StringView module)>;

        HotReload(lua::State& state, ModuleLoader& loader) noexcept;

        HotReload(const HotReload&) = delete;
        HotReload& operator=(const HotReload&) = delete;
        HotReload(HotReload&&) = delete;
        HotReload& operator=(HotReload&&) = delete;

        /// Installs the patch helper into a freshly opened Lua state.
        void Init(ModuleReloaded onModuleReloaded);
        void Shutdown() noexcept;

        /// Runs a loaded module again and patches its table in place.
        [[nodiscard]] auto Reload(StringView name) -> EngineResult<void>;
        /// Reloads every module whose file differs from the running version; returns how many were reloaded.
        size_t ReloadChanged();
        /// Checks the module files a few times per second and reloads the ones that changed.
        void Poll();

    private:
        /// Runs the new version and puts it in place of the old one. The result is the error of
        /// its on_reload, empty when that passed. Leaves values on the stack.
        [[nodiscard]] auto Replace(const String& name, const Path& file) -> EngineResult<String>;
        /// Runs the module file as require does and pushes its value (true for nil).
        [[nodiscard]] auto RunAgain(const String& name, const Path& file) -> EngineResult<void>;
        /// Copies the fresh table into the previous one and calls on_reload(previous fields).
        [[nodiscard]] auto Patch(int previous, int fresh) -> EngineResult<String>;
        [[nodiscard]] auto Fail(const String& name, const String& message) -> std::unexpected<EngineError>;
        void Finish(const String& name, const String& hookError);

        lua::State& m_state;
        ModuleLoader& m_loader;
        ModuleReloaded m_onModuleReloaded;
        std::chrono::steady_clock::time_point m_nextPoll { };
    };

} // namespace elm::script
