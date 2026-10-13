#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Settings.hpp"

#include "script/lua/Bind.hpp"

#include <chrono>

namespace elm {
    class Camera;
    class InputSystem;
    class Scene;
    namespace graphics {
        class ModelManager;
    }
}

namespace elm::script {

    /// Engine systems the bindings work with. Any of them may be missing (tools, tests):
    /// the functions that need it raise a Lua error then.
    struct EngineContext {
        Scene* scene { nullptr };
        graphics::ModelManager* models { nullptr };
        InputSystem* input { nullptr };
        Camera* camera { nullptr };
        Settings* settings { nullptr };
    };

    /// Frame timing as scripts see it (elm.time).
    struct ScriptTime {
        double time { 0.0 };
        float deltaTime { 0.0f };
        float fixedDeltaTime { 0.0f };
        uint64_t frame { 0 };
    };

    /// Runs Lua scripts on the update thread. Scripts live in assets/scripts and are loaded with
    /// require("a.b") -> assets/scripts/a/b.lua (or a/b/init.lua). The main module (setting
    /// Script/MainModule) may return a table with hooks:
    ///     on_start(), on_update(dt), on_fixed_update(dt), on_shutdown(), on_reload(previous)
    /// A hook that fails stops the main module's hooks until it is reloaded or the scripts restart.
    ///
    /// Hot reload: a changed module file is run again and its new table is copied into the old one,
    /// so everything holding the module (require results, the main module) sees the new functions;
    /// the module's own upvalues that held the new table (local M = {}) are pointed at the old one.
    /// Fields absent from the new version are removed; state that must survive lives in
    /// elm.script.persist() or is taken over in on_reload(previous) from the old fields.
    /// A module that fails to load keeps its previous version running.
    class ScriptSystem {
    public:
        static constexpr StringView EnabledSetting = "Enabled";
        static constexpr StringView MainModuleSetting = "MainModule";
        static constexpr StringView HotReloadSetting = "HotReload";
        /// Function of the main module that builds the scene ("Rebuild scene" in the Scripts window).
        static constexpr const char* BuildSceneFunction = "build_scene";

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

        enum class MainState { Idle, Running, Faulted };

        ScriptSystem() = default;
        ~ScriptSystem();

        ScriptSystem(const ScriptSystem&) = delete;
        ScriptSystem& operator=(const ScriptSystem&) = delete;

        static void RegisterSettings(Settings& settings);

        /// The systems the bindings reach; set before Start, may change at any time.
        void SetEngineContext(const EngineContext& context) noexcept { m_engine = context; }
        [[nodiscard]] const EngineContext& Engine() const noexcept { return m_engine; }
        [[nodiscard]] const ScriptTime& Time() const noexcept { return m_time; }

        /// The ScriptSystem a Lua state belongs to.
        [[nodiscard]] static ScriptSystem& From(lua_State* L) noexcept { return *lua::State::Context<ScriptSystem>(L); }

        /// Creates the Lua state and registers the engine modules. scriptRoot defaults to AssetPaths::Scripts().
        [[nodiscard]] auto Init(Path scriptRoot = { }) -> EngineResult<void>;
        /// Requires the main module and calls its on_start. A missing main module is not an error.
        [[nodiscard]] auto Start(const Settings& settings) -> EngineResult<void>;
        /// Applies a requested restart, reloads changed files (setting Script/HotReload), calls on_update.
        void Update(float deltaTime);
        void FixedUpdate(float fixedDeltaTime);
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept { return m_state != nullptr; }
        [[nodiscard]] lua::State& GetState() noexcept { return *m_state; }
        [[nodiscard]] const Path& ScriptRoot() const noexcept { return m_scriptRoot; }
        /// Modules loaded through require since Init, in load order.
        [[nodiscard]] const Vector<LoadedModule>& LoadedModules() const noexcept { return m_loadedModules; }

        [[nodiscard]] MainState GetMainState() const noexcept;
        [[nodiscard]] const String& MainModuleName() const noexcept { return m_mainModuleName; }
        /// Why the main module stopped, empty unless it is faulted.
        [[nodiscard]] const String& MainError() const noexcept { return m_mainError; }

        /// require(name) in protected mode, the module value is left on the stack.
        [[nodiscard]] auto Require(StringView name) -> EngineResult<void>;
        /// Path of the file a module name maps to, empty when no such file exists.
        [[nodiscard]] Path FindModule(StringView name) const;
        /// Module name of a script file: assets/scripts/a/b.lua -> "a.b", a/init.lua -> "a";
        /// empty for a file outside the script root or not a .lua file.
        [[nodiscard]] String ModuleNameOf(const Path& file) const;
        /// Calls main.<function>() in protected mode; false when the main module has no such function.
        /// A failure is returned, it does not stop the main module.
        [[nodiscard]] auto CallMain(const char* function) -> EngineResult<bool>;

        // ── Hot reload ──────────────────────────────────────────────────────────

        /// Runs a loaded module again and patches its table in place (see the class comment).
        [[nodiscard]] auto ReloadModule(StringView name) -> EngineResult<void>;
        /// Reloads every module whose file differs from the running version; returns how many were reloaded.
        size_t ReloadChanged();
        /// Checks the module files a few times per second and reloads the ones that changed.
        void PollFileChanges();
        /// Starts the scripts from a fresh Lua state on the next Update; safe to call from Lua.
        void RequestRestart() noexcept { m_restartRequested = true; }
        /// Shutdown + Init + Start now. Not from inside a Lua call: use RequestRestart there.
        [[nodiscard]] auto Restart() -> EngineResult<void>;

        // ── Console ─────────────────────────────────────────────────────────────

        /// Runs a line of Lua. An expression gives its values as text, a statement just runs;
        /// print() output of the command is included.
        [[nodiscard]] auto Execute(StringView code) -> EngineResult<String>;
        /// Completions of the last name in text ("elm.sc" -> "elm.scene"), each as the whole new text.
        /// Looks through tables and class userdata with raw access only, so it never runs script code.
        [[nodiscard]] Vector<String> Complete(StringView text);
        /// Collects print() output while a console command runs, nullptr otherwise.
        [[nodiscard]] String* ConsoleCapture() noexcept { return m_consoleCapture; }

    private:
        void InstallModuleSearcher();
        void InstallHelpers();
        void CallHook(const char* name, float deltaTime, bool passDeltaTime);
        [[nodiscard]] bool HotReloadEnabled() const;
        [[nodiscard]] LoadedModule* FindLoaded(StringView name);
        [[nodiscard]] String ChunkName(const Path& path) const;

        static int SearchModule(lua_State* L);

        UniquePtr<lua::State> m_state;
        Path m_scriptRoot;
        Vector<LoadedModule> m_loadedModules;
        EngineContext m_engine;
        ScriptTime m_time;
        lua::Ref m_mainModule;
        String m_mainModuleName;
        String m_mainError;
        bool m_mainFaulted { false };
        bool m_restartRequested { false };
        String* m_consoleCapture { nullptr };
        std::chrono::steady_clock::time_point m_nextPoll { };
    };

} // namespace elm::script
