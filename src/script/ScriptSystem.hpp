#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "script/EngineContext.hpp"
#include "script/lua/State.hpp"

#include <span>

namespace elm {
    class IImGuiWindow;
    class Settings;
    struct CollisionEvent;
}

namespace elm::script {

    class EntityScripts;
    class HotReload;
    class MainModule;
    class ModuleLoader;
    class ModulePaths;
    class ScriptClass;
    class ScriptConsole;
    class ScriptDefaults;

    /// Runs the Lua scripts of the engine: opens the Lua state, wires the script parts and the
    /// bindings, drives them through the frame and shuts them down in order. The parts do the work
    /// and stay inside the script module:
    ///  - ModulePaths, ModuleLoader: where modules live, require;
    ///  - HotReload: reloading changed modules; MainModule: the hooks of the main module;
    ///  - ScriptClass, EntityScripts, ScriptDefaults: the scripts of scene nodes;
    ///  - ScriptConsole: the Lua console; bindings/: the engine API for Lua.
    /// The parts live as long as the system, the Lua state is opened again on a restart.
    /// Owner: EngineApp, thread: update.
    class ScriptSystem {
    public:
        static constexpr StringView EnabledSetting = "Enabled";

        ScriptSystem();
        ~ScriptSystem();

        ScriptSystem(const ScriptSystem&) = delete;
        ScriptSystem& operator=(const ScriptSystem&) = delete;
        ScriptSystem(ScriptSystem&&) = delete;
        ScriptSystem& operator=(ScriptSystem&&) = delete;

        static void RegisterSettings(Settings& settings);
        /// The LuaLS definitions (---@meta) of everything the bindings register.
        [[nodiscard]] static auto GenerateApiMeta() -> EngineResult<String>;

        /// Opens the Lua state and registers the engine modules for these systems, any of which may
        /// be missing (tools, tests). Modules are found under scriptRoot, the project's scripts folder;
        /// without it no module file is found.
        [[nodiscard]] auto Init(const EngineContext& engine, Path scriptRoot = { }) -> EngineResult<void>;
        /// Starts the main module (on_start), the entry of the project; Play does it after Reset. An empty
        /// name or a missing module file is not an error: the scripts run without a main module.
        [[nodiscard]] auto Start(StringView mainModule) -> EngineResult<void>;
        /// A fresh Lua state for the same systems and scripts folder: Shutdown, then Init. Play and Stop
        /// start from it, so the game always starts as it would in a player.
        [[nodiscard]] auto Reset() -> EngineResult<void>;
        /// Applies a requested restart, reloads changed files (setting Script/HotReload), calls on_update.
        void Update(float deltaTime);
        /// After a physics step: the collisions of the step, then on_fixed_update.
        void FixedUpdate(float fixedDeltaTime, std::span<const CollisionEvent> collisions = { });
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept { return m_state.IsOpen(); }

        /// Edit -> Play (PlayMode calls it): the main module gets on_play, entity scripts start on the next update.
        void OnPlay();
        /// Play -> Edit: entity scripts end (on_destroy), the main module gets on_stop. Not from inside a Lua call.
        void OnStop();

        /// The Scripts window, wired to the parts of this system. After Init: it takes the Play requests of
        /// the engine context and gives the other windows PlayControls and ScriptInspection.
        [[nodiscard]] Vector<UniquePtr<IImGuiWindow>> CreateEditorWindows(Settings& settings);

    private:
        friend struct SelfTestAccess;

        void RegisterBindings();
        /// Reset now, on the update after the request; in Play the main module starts again.
        [[nodiscard]] auto Restart() -> EngineResult<void>;
        [[nodiscard]] bool HotReloadEnabled() const;
        /// What a module reload changes for the other parts.
        void OnModuleReloaded(StringView name);

        // The Lua state is destroyed last: the parts below hold references into it
        lua::State m_state;
        UniquePtr<ModulePaths> m_modulePaths;
        UniquePtr<ModuleLoader> m_moduleLoader;
        UniquePtr<MainModule> m_mainModule;
        UniquePtr<HotReload> m_hotReload;
        UniquePtr<ScriptClass> m_scriptClass;
        UniquePtr<ScriptDefaults> m_scriptDefaults;
        UniquePtr<EntityScripts> m_entityScripts;
        UniquePtr<ScriptConsole> m_console;

        EngineContext m_engine;
        ScriptTime m_time;
        /// The main module Start was given: a restart in Play starts it again.
        String m_mainModuleName { "main" };
        /// Asked for from Lua or the Scripts window, applied at the start of the next update.
        bool m_restartRequested { false };
        /// Setting Script/Enabled as Start saw it: entity scripts run only when scripts do.
        bool m_scriptsEnabled { true };
        /// The scripts run as in Play mode: main.on_update, entity scripts.
        bool m_playing { false };
    };

} // namespace elm::script
