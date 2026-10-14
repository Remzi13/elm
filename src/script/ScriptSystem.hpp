#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"
#include "core/Settings.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/ScriptComponent.hpp"

#include "script/lua/Bind.hpp"

#include <chrono>
#include <optional>
#include <span>

namespace elm {
    class Camera;
    class IImGuiWindow;
    class InputSystem;
    class PhysicsSystem;
    class Scene;
    class SceneSnapshot;
    struct CollisionEvent;
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
        PhysicsSystem* physics { nullptr };
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
    ///
    /// Entity scripts: every scene node with a ScriptComponent gets an instance of its module (a class
    /// table): self = { entity = <Entity>, <props> } with the module as __index. Hooks, all optional:
    ///     on_start(self), on_update(self, dt), on_fixed_update(self, dt), on_destroy(self), on_script_reload(self)
    ///     on_collision_enter(self, other, contact), on_collision_exit(self, other),
    ///     on_trigger_enter(self, other), on_trigger_exit(self, other)
    /// An error stops only that instance, until a module reload. Hot reload patches the class table,
    /// so the instances run the new code and keep their fields.
    class ScriptSystem {
    public:
        static constexpr StringView EnabledSetting = "Enabled";
        static constexpr StringView MainModuleSetting = "MainModule";
        static constexpr StringView HotReloadSetting = "HotReload";
        static constexpr StringView PlayOnStartSetting = "PlayOnStart";
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

        /// The instance of a node's ScriptComponent.
        struct EntityScript {
            NodeId node;
            /// The module the instance was made from: a component naming another one gets a new instance.
            String module;
            /// The instance table, empty when it could not be made (error tells why).
            lua::Ref self;
            /// Tells this instance from one that took its place during a Lua call.
            uint64_t serial { 0 };
            bool started { false };
            /// Stopped by an error until a module reloads.
            bool faulted { false };
            /// Gone: on_destroy was called, the entry is dropped after the running pass.
            bool dead { false };
            String error;
        };

        ScriptSystem() = default;
        ~ScriptSystem();

        ScriptSystem(const ScriptSystem&) = delete;
        ScriptSystem& operator=(const ScriptSystem&) = delete;

        static void RegisterSettings(Settings& settings);

        /// The systems the bindings reach; set before Start, may change at any time. Entity script
        /// instances of a scene that is replaced are dropped without on_destroy.
        void SetEngineContext(const EngineContext& context) noexcept;
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

        // ── Entity scripts ──────────────────────────────────────────────────────

        /// Pushes the instance table of the node's script, nil for a node without a ScriptComponent.
        /// A component without an instance gets one now; its on_start waits for the next Update.
        /// When the instance cannot be made nothing is pushed and the error is returned; keepFailure
        /// remembers it as a faulted instance, so it is not tried again before a module reload.
        [[nodiscard]] auto PushEntityScript(NodeId node, bool keepFailure = true) -> EngineResult<void>;
        /// Calls on_destroy of the started instances of these nodes, while the nodes still exist,
        /// and drops the instances. Before the nodes or their ScriptComponents are removed.
        void DestroyEntityScripts(std::span<const NodeId> nodes);
        /// The same for every instance: before the scene is cleared or replaced.
        void DestroyAllEntityScripts();
        /// on_collision_* / on_trigger_* of the scripts of both entities of every event (after a physics step).
        void DispatchCollisionEvents(std::span<const CollisionEvent> events);
        /// Instance of the node's script, nullptr when it has none.
        [[nodiscard]] const EntityScript* FindEntityScript(NodeId node) const noexcept;
        /// Instances, in the order their hooks run (dropped ones included until the pass ends).
        [[nodiscard]] std::span<const EntityScript> EntityScripts() const noexcept { return m_entityScripts; }

        // ── Play mode ───────────────────────────────────────────────────────────
        // Edit (the start): the scene is data. Entity scripts do not start, the main module gets
        // on_start and on_reload only, physics stands still (EngineApp steps it while IsPlaying).
        // Play keeps a snapshot of the scene and runs everything; the main module gets on_play.
        // Stop ends the entity scripts (on_destroy), calls main.on_stop, clears the physics bodies
        // and restores the snapshot with its node ids. Setting Script/PlayOnStart starts in Play.

        [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
        /// Now. Not from inside a Lua call: use RequestPlay/RequestStop there.
        void Play();
        void Stop();
        /// Play or Stop at the start of the next Update: safe from Lua and from the UI.
        void RequestPlay() noexcept { m_playRequest = PlayRequest::Play; }
        void RequestStop() noexcept { m_playRequest = PlayRequest::Stop; }

        // ── Inspector ───────────────────────────────────────────────────────────

        using ScriptProps = Vector<std::pair<String, ScriptValue>>;
        /// The `props` of a script class as prop values, sorted by name; the module is loaded for it.
        /// Cached until a module reloads or ForgetScriptDefaults; a module that does not load gives its error.
        [[nodiscard]] auto ScriptDefaults(StringView module) -> const EngineResult<ScriptProps>&;
        void ForgetScriptDefaults() noexcept { m_scriptDefaults.clear(); }
        /// Writes a prop into the node's script instance as well, so that a change shows at once.
        void SetEntityScriptField(NodeId node, StringView name, const ScriptValue& value);
        /// Names of the modules under the script root ("npc.guard"), sorted. Folders starting with '.' are skipped.
        [[nodiscard]] Vector<String> ListModules() const;

        // ── Console ─────────────────────────────────────────────────────────────

        /// Runs a line of Lua. An expression gives its values as text, a statement just runs;
        /// print() output of the command is included.
        [[nodiscard]] auto Execute(StringView code) -> EngineResult<String>;
        /// Completions of the last name in text ("elm.sc" -> "elm.scene"), each as the whole new text.
        /// Looks through tables and class userdata with raw access only, so it never runs script code.
        [[nodiscard]] Vector<String> Complete(StringView text);
        /// Collects print() output while a console command runs, nullptr otherwise.
        [[nodiscard]] String* ConsoleCapture() noexcept { return m_consoleCapture; }

        // ── UI ──────────────────────────────────────────────────────────────────

        /// The Scripts window (console, modules, reload) working with this system.
        [[nodiscard]] UniquePtr<IImGuiWindow> CreateEditorWindow(Settings& settings);
        /// The Inspector window: components of the node selected in Scene Hierarchy, Play/Stop.
        [[nodiscard]] UniquePtr<IImGuiWindow> CreateInspectorWindow(Settings& settings);

    private:
        void InstallModuleSearcher();
        void InstallHelpers();
        void CallHook(const char* name, float deltaTime, bool passDeltaTime);
        [[nodiscard]] bool HotReloadEnabled() const;
        [[nodiscard]] LoadedModule* FindLoaded(StringView name);
        [[nodiscard]] String ChunkName(const Path& path) const;

        static int SearchModule(lua_State* L);

        // Entity scripts (EntityScripts.cpp)
        void InstallEntityScriptHelpers();
        void UpdateEntityScripts(float deltaTime);
        void FixedUpdateEntityScripts(float fixedDeltaTime);
        /// on_destroy for the instances whose node or ScriptComponent went away behind their back.
        void ReapEntityScripts();
        /// Instances for the components that have none, then on_start for those not started.
        void StartEntityScripts();
        /// Makes the instance of a component and pushes its table; on failure nothing is pushed.
        [[nodiscard]] auto MakeEntityScript(NodeId node, const ScriptComponent& component) -> EngineResult<void>;
        /// Calls self:<hook>(...) in protected mode with the argumentCount values on top of the stack
        /// (they are popped); an error faults the instance.
        void CallEntityHook(size_t index, const char* hook, int argumentCount = 0);
        void FaultEntityScript(size_t index, uint64_t serial, const char* hook, const String& message);
        /// Marks the instance dead, then calls its on_destroy if it was started.
        void EndEntityScript(size_t index, bool callOnDestroy);
        [[nodiscard]] size_t FindEntityScriptIndex(NodeId node) const noexcept;
        /// Removes the dead instances, unless a pass over them is running.
        void DropDeadEntityScripts();
        /// A module reloaded: faulted instances get another chance, on_script_reload for its instances.
        void OnModuleReloaded(StringView name);
        /// Forgets every instance without calling hooks.
        void ReleaseEntityScripts() noexcept;

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

        /// In creation order. Entries are only appended or marked dead while Lua runs, removal
        /// waits for DropDeadEntityScripts, so a pass over them can go by index.
        Vector<EntityScript> m_entityScripts;
        /// NodeId::Raw() -> index in m_entityScripts of the node's latest instance.
        UnorderedMap<uint32_t, uint32_t> m_entityScriptIndex;
        uint64_t m_nextEntitySerial { 1 };
        /// Setting Script/Enabled as Start saw it: entity scripts run only when scripts do.
        bool m_scriptsEnabled { true };
        /// Running passes over m_entityScripts (they nest through Lua calls).
        uint32_t m_entityPassDepth { 0 };
        bool m_hasDeadEntityScripts { false };
        /// Errors of entity scripts already logged since the last module reload: a bug shared by
        /// many instances is reported once.
        Set<String> m_reportedEntityErrors;

        enum class PlayRequest : uint8_t { None, Play, Stop };
        bool m_playing { false };
        PlayRequest m_playRequest { PlayRequest::None };
        /// The scene as Play found it, null in Edit mode.
        UniquePtr<SceneSnapshot> m_snapshot;
        /// Module name -> ScriptDefaults result.
        UnorderedMap<String, EngineResult<ScriptProps>> m_scriptDefaults;
    };

} // namespace elm::script
