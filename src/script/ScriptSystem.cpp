#include "script/ScriptSystem.hpp"

#include "core/AssetPaths.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Settings.hpp"
#include "core/Unexpected.hpp"

#include "script/bindings/Bindings.hpp"
#include "script/console/ScriptConsole.hpp"
#include "script/entity/EntityScripts.hpp"
#include "script/entity/ScriptClass.hpp"
#include "script/entity/ScriptDefaults.hpp"
#include "script/module/MainModule.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/module/ModulePaths.hpp"
#include "script/reload/HotReload.hpp"
#include "script/ui/InspectorWindow.hpp"
#include "script/ui/ScriptWindow.hpp"

#include <utility>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptSystem";

    } // namespace

    ScriptSystem::ScriptSystem()
        : m_modulePaths(MakeUnique<ModulePaths>())
        , m_moduleLoader(MakeUnique<ModuleLoader>(m_state, *m_modulePaths))
        , m_mainModule(MakeUnique<MainModule>(m_state, *m_moduleLoader))
        , m_hotReload(MakeUnique<HotReload>(m_state, *m_moduleLoader))
        , m_scriptClass(MakeUnique<ScriptClass>(m_state, *m_moduleLoader))
        , m_scriptDefaults(MakeUnique<ScriptDefaults>(*m_scriptClass))
        , m_entityScripts(MakeUnique<EntityScripts>(m_state, *m_scriptClass))
        , m_console(MakeUnique<ScriptConsole>(m_state))
    {
        // Init opens the Lua state
        m_state.Close();
    }

    ScriptSystem::~ScriptSystem()
    {
        Shutdown();
    }

    void ScriptSystem::RegisterSettings(Settings& settings)
    {
        settings.Register(Settings::Category::Script, EnabledSetting, "Run scripts", true);
        settings.Register(Settings::Category::Script, MainModule::NameSetting, "Main module", String("main"));
        settings.Register(Settings::Category::Script, HotReload::EnabledSetting, "Reload changed scripts", true);
    }

    auto ScriptSystem::Init(const EngineContext& engine, Path scriptRoot) -> EngineResult<void>
    {
        ELM_ASSERT_THREAD(Update);
        if (m_state.IsOpen())
            return { };

        m_engine = engine;
        m_modulePaths->SetRoot(scriptRoot.Empty() ? AssetPaths::Scripts() : std::move(scriptRoot));
        m_state.Reset();
        m_state.OpenLibraries();
        m_moduleLoader->Init();
        m_hotReload->Init([this](StringView module) { OnModuleReloaded(module); });
        m_scriptClass->Init();
        m_entityScripts->Init(m_engine.scene);
        RegisterBindings();
        // After the bindings: the console wraps their print
        m_console->Init();

        LOG_MESSAGE(log::Category::Script, LogSource, "%s initialized, scripts in %s", LUA_RELEASE,
            m_modulePaths->Root().string().c_str());
        return { };
    }

    auto ScriptSystem::Start(const Settings& settings) -> EngineResult<void>
    {
        if (!m_state.IsOpen())
            return MakeUnexpected(ErrorCode::ScriptError, "ScriptSystem::Start called before Init", log::Category::Script, LogSource);
        m_scriptsEnabled = settings.Get<bool>(Settings::Category::Script, EnabledSetting, true);
        if (!m_scriptsEnabled)
            return { };
        return m_mainModule->Start(settings.Get<String>(Settings::Category::Script, MainModule::NameSetting, String("main")));
    }

    void ScriptSystem::Update(float deltaTime)
    {
        ELM_PROFILE_SCOPE_N("Scripts Update");
        if (m_restartRequested) {
            if (auto restarted = Restart(); !restarted)
                ERROR_MESSAGE(log::Category::Script, LogSource, "Restart failed: %s", restarted.error().message.c_str());
        }
        if (HotReloadEnabled())
            m_hotReload->Poll();

        m_time.deltaTime = deltaTime;
        m_time.time += deltaTime;
        ++m_time.frame;
        if (m_playing)
            m_mainModule->CallHook("on_update", deltaTime);
        if (!m_scriptsEnabled)
            return;
        // After the main module: entities it spawned start and update in the same frame.
        // In Edit mode the instances wait for Play, only those that lost their node end.
        if (m_playing)
            m_entityScripts->Update(deltaTime);
        else
            m_entityScripts->Reap();
    }

    void ScriptSystem::FixedUpdate(float fixedDeltaTime, std::span<const CollisionEvent> collisions)
    {
        ELM_PROFILE_SCOPE_N("Scripts FixedUpdate");
        m_time.fixedDeltaTime = fixedDeltaTime;
        if (!m_playing)
            return;
        if (m_scriptsEnabled)
            m_entityScripts->DispatchCollisions(collisions);
        m_mainModule->CallHook("on_fixed_update", fixedDeltaTime);
        if (m_scriptsEnabled)
            m_entityScripts->FixedUpdate(fixedDeltaTime);
    }

    void ScriptSystem::Shutdown()
    {
        if (!m_state.IsOpen())
            return;
        ELM_ASSERT_THREAD(Update);

        m_mainModule->CallHook("on_shutdown");
        // The scene stays: a restart makes the instances again from its ScriptComponents
        m_entityScripts->DestroyAll();
        // References go before the state they point into
        m_entityScripts->Shutdown();
        m_scriptDefaults->Forget();
        m_mainModule->Shutdown();
        m_hotReload->Shutdown();
        m_moduleLoader->Shutdown();
        m_state.Close();
        m_restartRequested = false;
        m_time = { };
    }

    void ScriptSystem::OnPlay()
    {
        ELM_ASSERT_THREAD(Update);
        m_playing = true;
        m_mainModule->CallHook("on_play");
    }

    void ScriptSystem::OnStop()
    {
        ELM_ASSERT_THREAD(Update);
        ELM_ASSERT(!m_entityScripts->IsPassRunning() && "Stop from inside a Lua call: request it instead");
        m_entityScripts->DestroyAll();
        m_mainModule->CallHook("on_stop");
        m_playing = false;
        // Instances made by those calls go too: none runs in Edit mode
        m_entityScripts->Release();
    }

    auto ScriptSystem::GenerateApiMeta() -> EngineResult<String>
    {
        ScriptSystem scripts;
        if (auto init = scripts.Init({ }); !init)
            return MakeUnexpected(init.error());
        return scripts.m_state.Api().GenerateLuaMeta();
    }

    Vector<UniquePtr<IImGuiWindow>> ScriptSystem::CreateEditorWindows(Settings& settings)
    {
        const ScriptWindowDesc scripts { m_state, *m_console, *m_modulePaths, *m_moduleLoader, *m_hotReload, *m_mainModule,
            *m_entityScripts, [this] { m_restartRequested = true; } };
        const InspectorWindowDesc inspector { *m_entityScripts, *m_scriptDefaults, *m_modulePaths, *m_hotReload, m_engine.play,
            m_engine.physics };
        Vector<UniquePtr<IImGuiWindow>> windows;
        windows.push_back(MakeUnique<ScriptWindow>(settings, scripts));
        windows.push_back(MakeUnique<InspectorWindow>(settings, inspector));
        return windows;
    }

    auto ScriptSystem::Restart() -> EngineResult<void>
    {
        m_restartRequested = false;
        const Path root = m_modulePaths->Root();
        Shutdown();
        if (auto init = Init(m_engine, root); !init)
            return init;
        LOG_MESSAGE(log::Category::Script, LogSource, "Scripts restarted");
        if (m_engine.settings)
            return Start(*m_engine.settings);
        Settings defaults;
        RegisterSettings(defaults);
        return Start(defaults);
    }

    bool ScriptSystem::HotReloadEnabled() const
    {
        return !m_engine.settings || m_engine.settings->Get<bool>(Settings::Category::Script, HotReload::EnabledSetting, true);
    }

    void ScriptSystem::OnModuleReloaded(StringView name)
    {
        m_entityScripts->OnModuleReloaded(name);
        m_scriptDefaults->Forget();
        m_mainModule->OnModuleReloaded(name);
    }

    void ScriptSystem::RegisterBindings()
    {
        // In the order of the API description (assets/scripts/.meta/elm.lua)
        bindings::RegisterLog(m_state);
        bindings::RegisterTime(m_state, m_time);
        bindings::RegisterScriptModule(m_state, *m_moduleLoader, *m_hotReload, [this] { m_restartRequested = true; });
        bindings::RegisterSettingsModule(m_state, m_engine.settings);
        bindings::RegisterMath(m_state);
        bindings::RegisterInput(m_state, m_engine.input);
        bindings::RegisterScene(m_state, m_engine.scene, m_engine.models, *m_entityScripts);
        bindings::RegisterModels(m_state, m_engine.models);
        bindings::RegisterCamera(m_state, m_engine.camera);
        bindings::RegisterEngine(m_state, { m_engine.scene, m_engine.models, m_entityScripts.get(), m_engine.play });
        bindings::RegisterPhysics(m_state, m_engine.physics, m_engine.scene, m_engine.models);
    }

} // namespace elm::script
