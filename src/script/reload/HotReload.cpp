#include "script/reload/HotReload.hpp"

#include "script/lua/Stack.hpp"
#include "script/lua/State.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/module/ModulePaths.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "HotReload";
        constexpr auto PollInterval = std::chrono::milliseconds(250);

        /// Registry key of the patch helper.
        constexpr const char* PatchKey = "elm.hot_patch";

        /// patch(old, new) -> previous: copies the new module table into the old one (old keeps its
        /// identity) and returns a shallow copy of the old fields. References from the new table to
        /// itself (Class.__index = Class) are redirected to the old one.
        constexpr const char* PatchSource = R"(
            return function(old, new)
                local previous = {}
                for key, value in next, old do previous[key] = value end
                for key in next, previous do
                    if rawget(new, key) == nil then rawset(old, key, nil) end
                end
                for key, value in next, new do
                    rawset(old, key, value == new and old or value)
                end
                local meta = getmetatable(new)
                if meta ~= nil then setmetatable(old, meta) end
                return previous
            end
        )";

        /// The functions of a reloaded module still see its new table through their upvalues
        /// (`local M = {}`): point those at the old table, which everyone else holds. The upvalue
        /// is shared by every closure of the chunk, so fixing it once through any function fixes all.
        void RedirectUpvalues(lua_State* L, int target, int replaced)
        {
            target = lua_absindex(L, target);
            replaced = lua_absindex(L, replaced);
            lua_pushnil(L);
            while (lua_next(L, target)) {
                if (lua_isfunction(L, -1)) {
                    for (int upvalue = 1; lua_getupvalue(L, -1, upvalue); ++upvalue) {
                        const bool matches = lua_rawequal(L, -1, replaced);
                        lua_pop(L, 1);
                        if (matches) {
                            lua_pushvalue(L, target);
                            lua_setupvalue(L, -2, upvalue);
                        }
                    }
                }
                lua_pop(L, 1);
            }
        }

    } // namespace

    HotReload::HotReload(lua::State& state, ModuleLoader& loader) noexcept
        : m_state(state)
        , m_loader(loader)
    {
    }

    void HotReload::Init(ModuleReloaded onModuleReloaded)
    {
        m_onModuleReloaded = std::move(onModuleReloaded);
        const bool installed = m_state.InstallHelper(PatchKey, PatchSource).has_value();
        ELM_ASSERT(installed && "the patch helper is engine code and must load");
    }

    void HotReload::Shutdown() noexcept
    {
        m_onModuleReloaded = nullptr;
    }

    auto HotReload::Reload(StringView name) -> EngineResult<void>
    {
        if (!m_state.IsOpen())
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        LoadedModule* module = m_loader.Find(name);
        if (!module)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "module '" + String(name) + "' is not loaded"));
        module->writeTime = ModulePaths::WriteTimeOf(module->path);
        module->pendingWriteTime = 0;
        // Running the module may load others and move the entries: keep what is needed
        const String moduleName = module->name;
        const Path file = module->path;

        lua_State* L = m_state;
        const int top = lua_gettop(L);
        auto hookError = Replace(moduleName, file);
        lua_settop(L, top);
        if (!hookError)
            return Fail(moduleName, hookError.error().message);
        Finish(moduleName, *hookError);
        return { };
    }

    size_t HotReload::ReloadChanged()
    {
        Vector<String> changed;
        for (const auto& module : m_loader.LoadedModules()) {
            if (ModulePaths::WriteTimeOf(module.path) != module.writeTime)
                changed.push_back(module.name);
        }
        size_t reloaded = 0;
        for (const auto& name : changed) {
            if (Reload(name))
                ++reloaded;
        }
        return reloaded;
    }

    void HotReload::Poll()
    {
        if (!m_state.IsOpen())
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextPoll)
            return;
        m_nextPoll = now + PollInterval;

        // A change is applied once two polls in a row see the same file time: editors save in steps
        Vector<String> ready;
        for (auto& module : m_loader.LoadedModules()) {
            const int64_t writeTime = ModulePaths::WriteTimeOf(module.path);
            if (writeTime == 0 || writeTime == module.writeTime) {
                module.pendingWriteTime = 0;
                continue;
            }
            if (writeTime == module.pendingWriteTime)
                ready.push_back(module.name);
            else
                module.pendingWriteTime = writeTime;
        }
        for (const auto& name : ready)
            (void)Reload(name); // failures are logged and kept on the module
    }

    auto HotReload::Replace(const String& name, const Path& file) -> EngineResult<String>
    {
        if (auto ran = RunAgain(name, file); !ran)
            return MakeUnexpected(ran.error());
        lua_State* L = m_state;
        const int fresh = lua_gettop(L);
        luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        const int loaded = lua_gettop(L);
        lua_getfield(L, loaded, name.c_str());
        const int previous = lua_gettop(L);
        if (lua_istable(L, previous) && lua_istable(L, fresh))
            return Patch(previous, fresh);

        if (lua_istable(L, previous))
            WARNING_MESSAGE(log::Category::Script, LogSource,
                "Module '%s' no longer returns a table: modules that required it keep the old value", name.c_str());
        lua_pushvalue(L, fresh);
        lua_setfield(L, loaded, name.c_str());
        return String();
    }

    auto HotReload::RunAgain(const String& name, const Path& file) -> EngineResult<void>
    {
        if (auto loaded = m_loader.LoadChunk(file); !loaded)
            return loaded;
        // Run like require does: (name, file) as arguments, nil means true
        lua_State* L = m_state;
        lua::Push(L, name);
        lua::Push(L, file.generic_string());
        if (auto ran = m_state.ProtectedCall(2, 1); !ran)
            return ran;
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        return { };
    }

    auto HotReload::Patch(int previous, int fresh) -> EngineResult<String>
    {
        lua_State* L = m_state;
        lua_getfield(L, LUA_REGISTRYINDEX, PatchKey);
        lua_pushvalue(L, previous);
        lua_pushvalue(L, fresh);
        if (auto patched = m_state.ProtectedCall(2, 1); !patched)
            return MakeUnexpected(patched.error());
        RedirectUpvalues(L, previous, fresh);

        // on_reload of the new version, now in the old table, gets the previous fields
        lua_pushliteral(L, "on_reload");
        if (lua_rawget(L, previous) != LUA_TFUNCTION)
            return String();
        lua_pushvalue(L, -2);
        if (auto hook = m_state.ProtectedCall(1, 0); !hook)
            return hook.error().message;
        return String();
    }

    auto HotReload::Fail(const String& name, const String& message) -> std::unexpected<EngineError>
    {
        if (auto* module = m_loader.Find(name))
            module->error = message;
        ERROR_MESSAGE(log::Category::Script, LogSource, "Reload of '%s' failed, the previous version keeps running:\n%s",
            name.c_str(), message.c_str());
        return MakeUnexpected(EngineError(ErrorCode::ScriptError, message));
    }

    void HotReload::Finish(const String& name, const String& hookError)
    {
        if (auto* module = m_loader.Find(name)) {
            module->error = hookError;
            ++module->reloadCount;
        }
        if (!hookError.empty())
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s.on_reload failed:\n%s", name.c_str(), hookError.c_str());
        LOG_MESSAGE(log::Category::Script, LogSource, "Reloaded '%s'", name.c_str());
        if (m_onModuleReloaded)
            m_onModuleReloaded(name);
    }

} // namespace elm::script
