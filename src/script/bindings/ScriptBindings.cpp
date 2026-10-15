#include "script/bindings/Bindings.hpp"

#include "script/module/ModuleLoader.hpp"
#include "script/reload/HotReload.hpp"

#include "core/Memory.hpp"

namespace elm::script::bindings {

    namespace {

        /// What elm.script reaches.
        struct ScriptModuleContext {
            ModuleLoader* modules { nullptr };
            HotReload* hotReload { nullptr };
            /// Restarts the scripts on the next update, empty where nothing can restart them.
            std::function<void()> requestRestart;
        };

        ScriptModuleContext& Context(lua_State* L) { return ContextOf<ScriptModuleContext>(L); }

        EngineResult<void> Reload(lua_State* L, StringView name) { return Context(L).hotReload->Reload(name); }
        size_t ReloadChanged(lua_State* L) { return Context(L).hotReload->ReloadChanged(); }

        void Restart(lua_State* L)
        {
            const auto& restart = Context(L).requestRestart;
            if (!restart)
                luaL_error(L, "elm.script.restart is not available here");
            restart();
        }

        Vector<String> Modules(lua_State* L)
        {
            Vector<String> names;
            for (const auto& module : Context(L).modules->LoadedModules())
                names.push_back(module.name);
            return names;
        }

        /// persist(key, default) -> the table stored under key, created from default (or {}) the
        /// first time. Survives module reloads, not restarts: module state that must outlive a reload.
        int Persist(lua_State* L)
        {
            const char* key = luaL_checkstring(L, 1);
            luaL_getsubtable(L, LUA_REGISTRYINDEX, "elm.persist");
            if (lua_getfield(L, -1, key) != LUA_TNIL)
                return 1;
            lua_pop(L, 1);
            if (lua_isnoneornil(L, 2))
                lua_newtable(L);
            else
                lua_pushvalue(L, 2);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, key);
            return 1;
        }

        size_t LuaMemory(lua_State* L) { return lua::State::From(L).MemoryUsage(); }
        int64_t EngineMemory() { return memory::getStatistic().allocated; }

    } // namespace

    void RegisterScriptModule(lua::State& state, ModuleLoader& modules, HotReload& hotReload, std::function<void()> requestRestart)
    {
        state.SetContext(ScriptModuleContext { &modules, &hotReload, std::move(requestRestart) });
        lua::Module(state, "elm.script")
            .Function<&Reload>("reload", { "module" })
            .Function<&ReloadChanged>("reload_changed")
            .Function<&Restart>("restart")
            .Function<&Modules>("modules")
            .Function<&Persist>("persist", { "key", "default" })
            .Function<&LuaMemory>("memory")
            .Function<&EngineMemory>("engine_memory");
    }

} // namespace elm::script::bindings
