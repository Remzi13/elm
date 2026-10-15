#include "script/module/MainModule.hpp"

#include "script/module/ModuleLoader.hpp"
#include "script/module/ModulePaths.hpp"

#include "core/Log.hpp"
#include "core/Debug.hpp"
#include "core/Unexpected.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "MainModule";

    } // namespace

    MainModule::MainModule(lua::State& state, ModuleLoader& loader) noexcept
        : m_state(state)
        , m_loader(loader)
    {
    }

    auto MainModule::Start(StringView name) -> EngineResult<void>
    {
        m_name = String(name);
        if (m_name.empty() || m_loader.Paths().Find(m_name).Empty()) {
            LOG_MESSAGE(log::Category::Script, LogSource, "No main module '%s', scripts are idle", m_name.c_str());
            return { };
        }

        lua_State* L = m_state;
        if (auto required = m_loader.Require(m_name); !required)
            return MakeUnexpected(ErrorCode::ScriptError, required.error().message, log::Category::Script, LogSource);
        m_module = lua::Ref(L, -1);
        lua_pop(L, 1);
        m_faulted = false;
        m_error.clear();

        CallHook("on_start");
        return { };
    }

    void MainModule::Shutdown() noexcept
    {
        m_module.Reset();
        m_faulted = false;
        m_error.clear();
    }

    void MainModule::CallHook(const char* hook)
    {
        if (!m_state.IsOpen())
            return;
        lua::StackGuard guard(m_state);
        if (PushHook(hook))
            RunHook(hook, 0);
    }

    void MainModule::CallHook(const char* hook, float deltaTime)
    {
        if (!m_state.IsOpen())
            return;
        lua::StackGuard guard(m_state);
        if (!PushHook(hook))
            return;
        lua_pushnumber(m_state, deltaTime);
        RunHook(hook, 1);
    }

    auto MainModule::CallFunction(const char* function) -> EngineResult<bool>
    {
        if (!m_state.IsOpen() || !m_module)
            return false;
        lua::StackGuard guard(m_state);
        if (!PushFunction(function))
            return false;
        if (auto called = m_state.ProtectedCall(0, 0); !called)
            return MakeUnexpected(called.error());
        return true;
    }

    void MainModule::OnModuleReloaded(StringView name)
    {
        if (m_name.empty() || name != m_name || !m_state.IsOpen())
            return;
        m_faulted = false;
        m_error.clear();

        // A module that does not return a table is replaced on reload, not patched: take the new value
        lua_State* L = m_state;
        luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        lua_getfield(L, -1, m_name.c_str());
        const bool firstStart = !m_module;
        m_module = lua::Ref(L, -1);
        lua_pop(L, 2);
        if (firstStart)
            CallHook("on_start");
    }

    auto MainModule::GetState() const noexcept -> MainState
    {
        if (m_faulted)
            return MainState::Faulted;
        return m_module ? MainState::Running : MainState::Idle;
    }

    bool MainModule::PushHook(const char* hook)
    {
        if (!m_module || m_faulted)
            return false;
        ELM_ASSERT_THREAD(Update);
        return PushFunction(hook);
    }

    bool MainModule::PushFunction(const char* name)
    {
        lua_State* L = m_state;
        m_module.Push(L);
        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            return false;
        }
        // Raw access: a metamethod could raise an error outside a protected call
        lua_pushstring(L, name);
        if (lua_rawget(L, -2) != LUA_TFUNCTION) {
            lua_pop(L, 2);
            return false;
        }
        lua_remove(L, -2);
        return true;
    }

    void MainModule::RunHook(const char* hook, int argumentCount)
    {
        if (auto called = m_state.ProtectedCall(argumentCount, 0); !called) {
            m_faulted = true;
            m_error = String(hook) + ": " + called.error().message;
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s.%s failed, the main module is stopped until it is reloaded:\n%s",
                m_name.c_str(), hook, called.error().message.c_str());
        }
    }

} // namespace elm::script
