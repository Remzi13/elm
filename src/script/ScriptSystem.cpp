#include "script/ScriptSystem.hpp"

#include "script/bindings/Bindings.hpp"

#include "core/AssetPaths.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptSystem";

    } // namespace

    ScriptSystem::ScriptSystem()
        : m_modules(MakeUnique<ScriptModuleManager>())
        , m_entities(MakeUnique<EntityScriptManager>())
        , m_console(MakeUnique<ScriptConsole>())
        , m_reflection(MakeUnique<ScriptReflection>())
    {
        m_context.system = this;
        m_context.modules = m_modules.get();
        m_context.entities = m_entities.get();
        m_context.console = m_console.get();
        m_context.reflection = m_reflection.get();

        m_modules->SetOnReloaded([this](StringView name, bool isMain, bool startMain, const String& hookError) {
            OnModuleReloaded(name, isMain, startMain, hookError);
        });
    }

    ScriptSystem::~ScriptSystem()
    {
        Shutdown();
    }

    void ScriptSystem::RegisterSettings(Settings& settings)
    {
        settings.Register(Settings::Category::Script, EnabledSetting,
            "Runs the update thread scripts (assets/scripts)", true);
        settings.Register(Settings::Category::Script, MainModuleSetting,
            "Module required on start; hooks: on_start, on_update(dt), on_fixed_update(dt), on_shutdown, on_reload", String("main"));
        settings.Register(Settings::Category::Script, HotReloadSetting,
            "Reloads a script file when it is saved in an external editor or the Scripts window", true);
        settings.Register(Settings::Category::Script, PlayOnStartSetting,
            "Starts in Play mode: entity scripts and physics run, scene snapshot restored on stop", false);
    }

    void ScriptSystem::SetEngineContext(const EngineContext& context) noexcept
    {
        if (context.scene != m_context.engine.scene) {
            m_entities->ReleaseEntityScripts();
        }
        m_context.engine = context;
    }

    auto ScriptSystem::Init(Path scriptRoot) -> EngineResult<void>
    {
        ELM_ASSERT_THREAD(Update);
        if (m_state)
            return { };

        m_modules->SetScriptRoot(scriptRoot.Empty() ? AssetPaths::Scripts() : std::move(scriptRoot));
        m_state = MakeUnique<lua::State>();
        m_state->SetContext(&m_context);
        m_state->OpenLibraries();
        m_modules->Install(*m_state);
        m_console->InstallHelpers(*m_state);
        m_entities->InstallHelpers(*m_state);
        bindings::RegisterAll(*m_state);

        LOG_MESSAGE(log::Category::Script, LogSource, "%s initialized, scripts in %s", LUA_RELEASE,
            m_modules->ScriptRoot().string().c_str());
        return { };
    }

    auto ScriptSystem::Start(const Settings& settings) -> EngineResult<void>
    {
        if (!m_state)
            return MakeUnexpected(ErrorCode::ScriptError, "ScriptSystem::Start called before Init", log::Category::Script, LogSource);

        const String mainModule = settings.Get<String>(Settings::Category::Script, MainModuleSetting, String("main"));
        m_modules->SetMainModuleName(mainModule);
        m_scriptsEnabled = settings.Get<bool>(Settings::Category::Script, EnabledSetting, true);
        if (!m_scriptsEnabled)
            return { };

        if (mainModule.empty() || m_modules->FindModule(mainModule).Empty()) {
            LOG_MESSAGE(log::Category::Script, LogSource, "No main module '%s', scripts are idle", mainModule.c_str());
            return { };
        }

        lua_State* L = *m_state;
        if (auto required = Require(mainModule); !required)
            return MakeUnexpected(ErrorCode::ScriptError, required.error().message, log::Category::Script, LogSource);
        m_mainModule = lua::Ref(L, -1);
        lua_pop(L, 1);
        m_mainFaulted = false;
        m_mainError.clear();

        CallHook("on_start", 0.0f, false);
        return { };
    }

    void ScriptSystem::Update(float deltaTime)
    {
        ELM_PROFILE_SCOPE_N("Scripts Update");
        if (m_restartRequested) {
            if (auto restarted = Restart(); !restarted)
                ERROR_MESSAGE(log::Category::Script, LogSource, "Restart failed: %s", restarted.error().message.c_str());
        }

        if (HotReloadEnabled())
            m_modules->PollFileChanges(*m_state);

        m_context.time.deltaTime = deltaTime;
        m_context.time.time += deltaTime;
        ++m_context.time.frame;

        if (m_playing)
            CallHook("on_update", deltaTime, true);

        m_entities->Update(*m_state, m_context.engine.scene, *m_modules, m_playing, m_scriptsEnabled, deltaTime);
    }

    void ScriptSystem::FixedUpdate(float fixedDeltaTime)
    {
        ELM_PROFILE_SCOPE_N("Scripts FixedUpdate");
        m_context.time.fixedDeltaTime = fixedDeltaTime;
        if (!m_playing)
            return;
        CallHook("on_fixed_update", fixedDeltaTime, true);
        m_entities->FixedUpdate(*m_state, m_context.engine.scene, m_playing, m_scriptsEnabled, fixedDeltaTime);
    }

    void ScriptSystem::Shutdown()
    {
        if (!m_state)
            return;
        ELM_ASSERT_THREAD(Update);

        CallHook("on_shutdown", 0.0f, false);
        if (m_context.engine.scene)
            m_entities->DestroyAllEntityScripts(*m_state, m_context.engine.scene);
        m_entities->ReleaseEntityScripts();
        m_reflection->ForgetScriptDefaults();
        m_mainModule.Reset();
        m_state.reset();
        m_modules->Reset();
        m_mainFaulted = false;
        m_mainError.clear();
        m_restartRequested = false;
        m_context.time = { };
        m_playing = false;
    }

    auto ScriptSystem::Restart() -> EngineResult<void>
    {
        m_restartRequested = false;
        const Path root = m_modules->ScriptRoot();
        Shutdown();
        if (auto init = Init(root); !init)
            return init;
        LOG_MESSAGE(log::Category::Script, LogSource, "Scripts restarted");
        if (m_context.engine.settings)
            return Start(*m_context.engine.settings);
        Settings defaults;
        RegisterSettings(defaults);
        return Start(defaults);
    }

    auto ScriptSystem::GetMainState() const noexcept -> MainState
    {
        if (m_mainFaulted)
            return MainState::Faulted;
        return m_mainModule ? MainState::Running : MainState::Idle;
    }

    auto ScriptSystem::Require(StringView name) -> EngineResult<void>
    {
        auto result = m_modules->Require(*m_state, name);
        if (!result) {
            if (auto* module = m_modules->FindLoaded(name); module && module->error.empty())
                module->error = result.error().message;
        }
        return result;
    }

    auto ScriptSystem::ReloadModule(StringView name) -> EngineResult<void>
    {
        if (!m_state)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        return m_modules->ReloadModule(*m_state, name);
    }

    auto ScriptSystem::CallMain(const char* function) -> EngineResult<bool>
    {
        if (!m_state || !m_mainModule)
            return false;
        lua_State* L = *m_state;
        lua::StackGuard guard(L);
        m_mainModule.Push(L);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        lua_pushstring(L, function);
        if (lua_rawget(L, -2) != LUA_TFUNCTION) {
            lua_pop(L, 2);
            return false;
        }
        lua_remove(L, -2);
        if (auto called = m_state->ProtectedCall(0, 0); !called)
            return MakeUnexpected(called.error());
        return true;
    }

    void ScriptSystem::CallHook(const char* name, float deltaTime, bool passDeltaTime)
    {
        if (!m_state || !m_mainModule || m_mainFaulted)
            return;
        ELM_ASSERT_THREAD(Update);

        lua_State* L = *m_state;
        lua::StackGuard guard(L);
        m_mainModule.Push(L);
        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            return;
        }
        lua_pushstring(L, name);
        if (lua_rawget(L, -2) != LUA_TFUNCTION) {
            lua_pop(L, 2);
            return;
        }
        lua_remove(L, -2);

        if (passDeltaTime)
            lua_pushnumber(L, deltaTime);
        if (auto called = m_state->ProtectedCall(passDeltaTime ? 1 : 0, 0); !called) {
            m_mainFaulted = true;
            m_mainError = String(name) + ": " + called.error().message;
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s.%s failed, the main module is stopped until it is reloaded:\n%s",
                m_modules->MainModuleName().c_str(), name, called.error().message.c_str());
        }
    }

    void ScriptSystem::OnModuleReloaded(StringView name, bool isMain, bool startMain, const String& /*hookError*/)
    {
        m_reflection->Invalidate(name);
        if (m_state)
            m_entities->OnModuleReloaded(*m_state, name);

        if (isMain) {
            if (!m_mainModule && m_state) {
                lua_State* L = m_state->Get();
                luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
                lua_getfield(L, -1, m_modules->MainModuleName().c_str());
                if (!lua_isnil(L, -1))
                    m_mainModule = lua::Ref(L, -1);
                lua_pop(L, 2);
            }
            m_mainFaulted = false;
            m_mainError.clear();
        }
        if (startMain)
            CallHook("on_start", 0.0f, false);
    }

    bool ScriptSystem::HotReloadEnabled() const
    {
        return !m_context.engine.settings || m_context.engine.settings->Get<bool>(Settings::Category::Script, HotReloadSetting, true);
    }

    // ── Entity scripts delegation ───────────────────────────────────────────────

    auto ScriptSystem::PushEntityScript(NodeId node, bool keepFailure) -> EngineResult<void>
    {
        return m_entities->PushEntityScript(*m_state, m_context.engine.scene, *m_modules, node, keepFailure);
    }

    void ScriptSystem::DestroyEntityScripts(std::span<const NodeId> nodes)
    {
        if (m_state)
            m_entities->DestroyEntityScripts(*m_state, m_context.engine.scene, nodes);
    }

    void ScriptSystem::DestroyAllEntityScripts()
    {
        if (m_state)
            m_entities->DestroyAllEntityScripts(*m_state, m_context.engine.scene);
    }

    void ScriptSystem::DispatchCollisionEvents(std::span<const CollisionEvent> events)
    {
        if (m_state)
            m_entities->DispatchCollisionEvents(*m_state, m_context.engine.scene, events);
    }

    const EntityScript* ScriptSystem::FindEntityScript(NodeId node) const noexcept
    {
        return m_entities->FindEntityScript(node);
    }

    void ScriptSystem::SetEntityScriptField(NodeId node, StringView name, const ScriptValue& value)
    {
        if (m_state)
            m_entities->SetEntityScriptField(*m_state, node, name, value);
    }

    // ── Play mode lifecycle (invoked by PlayModeController) ──────────────────────

    void ScriptSystem::OnPlay()
    {
        m_playing = true;
        CallHook("on_play", 0.0f, false);
    }

    void ScriptSystem::OnStop()
    {
        if (m_state && m_context.engine.scene)
            m_entities->DestroyAllEntityScripts(*m_state, m_context.engine.scene);
        CallHook("on_stop", 0.0f, false);
        m_playing = false;
        m_entities->ReleaseEntityScripts();
    }

    // ── Reflection delegation ──────────────────────────────────────────────────

    auto ScriptSystem::ScriptDefaults(StringView module) -> const EngineResult<ScriptProps>&
    {
        return m_reflection->ScriptDefaults(m_state.get(), *m_modules, module);
    }

    // ── Console delegation ─────────────────────────────────────────────────────

    auto ScriptSystem::Execute(StringView code) -> EngineResult<String>
    {
        if (!m_state)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        return m_console->Execute(*m_state, code);
    }

    Vector<String> ScriptSystem::Complete(StringView text)
    {
        if (!m_state)
            return {};
        return m_console->Complete(*m_state, text);
    }

} // namespace elm::script
