#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Settings.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/ScriptComponent.hpp"

#include "script/EntityScriptManager.hpp"
#include "script/ScriptConsole.hpp"
#include "script/ScriptContext.hpp"
#include "script/ScriptModuleManager.hpp"
#include "script/ScriptReflection.hpp"
#include "script/lua/Bind.hpp"

#include <span>

namespace elm {
    class Camera;
    class InputSystem;
    class PhysicsSystem;
    class Scene;
    struct CollisionEvent;
    namespace graphics {
        class ModelManager;
    }
}

namespace elm::script {

    /// Runs Lua scripts on the update thread.
    /// Acts as a high-level facade coordinating the Lua state, module management,
    /// entity script lifecycle, console/REPL, and reflection.
    class ScriptSystem {
    public:
        static constexpr StringView EnabledSetting = "Enabled";
        static constexpr StringView MainModuleSetting = "MainModule";
        static constexpr StringView HotReloadSetting = "HotReload";
        static constexpr StringView PlayOnStartSetting = "PlayOnStart";
        /// Function of the main module that builds the scene ("Rebuild scene" in the Scripts window).
        static constexpr const char* BuildSceneFunction = "build_scene";

        using LoadedModule = script::LoadedModule;
        using EntityScript = script::EntityScript;
        using ScriptProps = script::ScriptProps;

        enum class MainState { Idle, Running, Faulted };

        ScriptSystem();
        ~ScriptSystem();

        ScriptSystem(const ScriptSystem&) = delete;
        ScriptSystem& operator=(const ScriptSystem&) = delete;

        static void RegisterSettings(Settings& settings);

        /// The systems the bindings reach; set before Start, may change at any time.
        void SetEngineContext(const EngineContext& context) noexcept;
        [[nodiscard]] const EngineContext& Engine() const noexcept { return m_context.engine; }
        [[nodiscard]] const ScriptTime& Time() const noexcept { return m_context.time; }

        /// The ScriptSystem a Lua state belongs to.
        [[nodiscard]] static ScriptSystem& From(lua_State* L) noexcept { return *ScriptContext::From(L).system; }

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

        // ── Subsystem accessors ──────────────────────────────────────────────────
        [[nodiscard]] ScriptModuleManager& Modules() noexcept { return *m_modules; }
        [[nodiscard]] const ScriptModuleManager& Modules() const noexcept { return *m_modules; }
        [[nodiscard]] EntityScriptManager& Entities() noexcept { return *m_entities; }
        [[nodiscard]] const EntityScriptManager& Entities() const noexcept { return *m_entities; }
        [[nodiscard]] ScriptConsole& Console() noexcept { return *m_console; }
        [[nodiscard]] const ScriptConsole& Console() const noexcept { return *m_console; }
        [[nodiscard]] ScriptReflection& Reflection() noexcept { return *m_reflection; }
        [[nodiscard]] const ScriptReflection& Reflection() const noexcept { return *m_reflection; }

        // ── Modules & Loading ───────────────────────────────────────────────────
        [[nodiscard]] const Path& ScriptRoot() const noexcept { return m_modules->ScriptRoot(); }
        [[nodiscard]] const Vector<LoadedModule>& LoadedModules() const noexcept { return m_modules->LoadedModules(); }
        [[nodiscard]] auto Require(StringView name) -> EngineResult<void>;
        [[nodiscard]] Path FindModule(StringView name) const { return m_modules->FindModule(name); }
        [[nodiscard]] String ModuleNameOf(const Path& file) const { return m_modules->ModuleNameOf(file); }
        [[nodiscard]] auto ReloadModule(StringView name) -> EngineResult<void>;
        size_t ReloadChanged() { return m_modules->ReloadChanged(*m_state); }
        void PollFileChanges() { m_modules->PollFileChanges(*m_state); }

        // ── Main Module ─────────────────────────────────────────────────────────
        [[nodiscard]] MainState GetMainState() const noexcept;
        [[nodiscard]] const String& MainModuleName() const noexcept { return m_modules->MainModuleName(); }
        [[nodiscard]] const String& MainError() const noexcept { return m_mainError; }
        [[nodiscard]] auto CallMain(const char* function) -> EngineResult<bool>;

        // ── Restart ─────────────────────────────────────────────────────────────
        void RequestRestart() noexcept { m_restartRequested = true; }
        [[nodiscard]] auto Restart() -> EngineResult<void>;

        // ── Entity scripts ──────────────────────────────────────────────────────
        [[nodiscard]] auto PushEntityScript(NodeId node, bool keepFailure = true) -> EngineResult<void>;
        void DestroyEntityScripts(std::span<const NodeId> nodes);
        void DestroyAllEntityScripts();
        void DispatchCollisionEvents(std::span<const CollisionEvent> events);
        [[nodiscard]] const EntityScript* FindEntityScript(NodeId node) const noexcept;
        [[nodiscard]] std::span<const EntityScript> EntityScripts() const noexcept { return m_entities->EntityScripts(); }

        // ── Play mode lifecycle (invoked by engine-level PlayModeController) ────
        [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
        void OnPlay();
        void OnStop();

        // ── Inspector & Reflection ──────────────────────────────────────────────
        [[nodiscard]] auto ScriptDefaults(StringView module) -> const EngineResult<ScriptProps>&;
        void ForgetScriptDefaults() noexcept { m_reflection->ForgetScriptDefaults(); }
        void SetEntityScriptField(NodeId node, StringView name, const ScriptValue& value);
        [[nodiscard]] Vector<String> ListModules() const { return m_modules->ListModules(); }

        // ── Console ─────────────────────────────────────────────────────────────
        [[nodiscard]] auto Execute(StringView code) -> EngineResult<String>;
        [[nodiscard]] Vector<String> Complete(StringView text);
        [[nodiscard]] String* ConsoleCapture() noexcept { return m_console->Capture(); }

    private:
        void CallHook(const char* name, float deltaTime, bool passDeltaTime);
        [[nodiscard]] bool HotReloadEnabled() const;
        void OnModuleReloaded(StringView name, bool isMain, bool startMain, const String& hookError);

        UniquePtr<lua::State> m_state;
        ScriptContext m_context;

        UniquePtr<ScriptModuleManager> m_modules;
        UniquePtr<EntityScriptManager> m_entities;
        UniquePtr<ScriptConsole> m_console;
        UniquePtr<ScriptReflection> m_reflection;

        lua::Ref m_mainModule;
        String m_mainError;
        bool m_mainFaulted { false };
        bool m_restartRequested { false };
        bool m_scriptsEnabled { true };
        bool m_playing { false };
    };

} // namespace elm::script
