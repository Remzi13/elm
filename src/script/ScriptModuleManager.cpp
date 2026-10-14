#include "script/ScriptModuleManager.hpp"
#include "script/ScriptContext.hpp"

#include "core/AssetPaths.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>
#include <iterator>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptModuleManager";
        constexpr auto PollInterval = std::chrono::milliseconds(250);

        constexpr const char* PatchKey = "elm.hot_patch";

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

        auto ReadTextFile(const Path& path) -> std::optional<String>
        {
            auto stream = FileSystem::OpenForReading(path, std::ios::in | std::ios::binary);
            if (!stream)
                return std::nullopt;
            String text { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
            if (text.starts_with("\xEF\xBB\xBF"))
                text.erase(0, 3);
            return text;
        }

        int64_t WriteTimeOf(const Path& path)
        {
            std::error_code error;
            return FileSystem::LastWriteTime(path, error);
        }

        bool IsValidModuleName(StringView name)
        {
            if (name.empty() || name.front() == '.' || name.back() == '.')
                return false;
            for (size_t index = 0; index < name.size(); ++index) {
                const char c = name[index];
                if (c == '/' || c == '\\' || c == ':' || (c == '.' && name[index + 1] == '.'))
                    return false;
            }
            return true;
        }

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

    ScriptModuleManager::ScriptModuleManager(Path scriptRoot)
        : m_scriptRoot(scriptRoot.Empty() ? AssetPaths::Scripts() : std::move(scriptRoot))
    {
    }

    void ScriptModuleManager::Install(lua::State& state)
    {
        lua_State* L = state.Get();

        // Install Patch helper
        const bool installed = state.Load(PatchSource, "=engine") && state.ProtectedCall(0, 1);
        ELM_ASSERT(installed);
        if (installed)
            lua_setfield(L, LUA_REGISTRYINDEX, PatchKey);

        // Install module searcher
        lua_getglobal(L, LUA_LOADLIBNAME);
        lua_getfield(L, -1, "searchers");
        lua_rawgeti(L, -1, 1);
        lua_newtable(L);
        lua_insert(L, -2);
        lua_rawseti(L, -2, 1);
        lua_pushcfunction(L, &ScriptModuleManager::SearchModule);
        lua_rawseti(L, -2, 2);
        lua_setfield(L, -3, "searchers");
        lua_pop(L, 2);
    }

    void ScriptModuleManager::Reset()
    {
        m_loadedModules.clear();
        m_mainModuleName.clear();
        m_nextPoll = {};
    }

    auto ScriptModuleManager::Require(lua::State& state, StringView name) -> EngineResult<void>
    {
        lua_State* L = state.Get();
        lua_getglobal(L, "require");
        lua::Push(L, name);
        return state.ProtectedCall(1, 1);
    }

    Path ScriptModuleManager::FindModule(StringView name) const
    {
        if (!IsValidModuleName(name))
            return { };
        String relative(name);
        for (char& c : relative) {
            if (c == '.')
                c = '/';
        }
        for (const char* suffix : { ".lua", "/init.lua" }) {
            Path candidate = m_scriptRoot / (relative + suffix);
            std::error_code error;
            if (FileSystem::IsRegularFile(candidate, error))
                return candidate;
        }
        return { };
    }

    String ScriptModuleManager::ModuleNameOf(const Path& file) const
    {
        const String relative(file.LexicallyRelative(m_scriptRoot).generic_string().c_str());
        if (relative.empty() || relative.starts_with("..") || !relative.ends_with(".lua") || Path(file).IsRelative())
            return { };
        String name = relative.substr(0, relative.size() - 4);
        if (name == "init")
            return { };
        if (name.ends_with("/init"))
            name.resize(name.size() - 5);
        for (char& c : name) {
            if (c == '/')
                c = '.';
        }
        return IsValidModuleName(name) ? name : String();
    }

    String ScriptModuleManager::ChunkName(const Path& path) const
    {
        return "@" + String(path.LexicallyRelative(m_scriptRoot).generic_string().c_str());
    }

    auto ScriptModuleManager::FindLoaded(StringView name) -> LoadedModule*
    {
        const auto it = std::find_if(m_loadedModules.begin(), m_loadedModules.end(),
            [&](const LoadedModule& module) { return module.name == name; });
        return it == m_loadedModules.end() ? nullptr : &*it;
    }

    auto ScriptModuleManager::ReloadModule(lua::State& state, StringView name) -> EngineResult<void>
    {
        LoadedModule* module = FindLoaded(name);
        if (!module)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "module '" + String(name) + "' is not loaded"));

        lua_State* L = state.Get();
        lua::StackGuard guard(L);
        module->writeTime = WriteTimeOf(module->path);
        module->pendingWriteTime = 0;
        const String moduleName = module->name;

        auto fail = [&](const String& message) -> std::unexpected<EngineError> {
            if (auto* failed = FindLoaded(moduleName))
                failed->error = message;
            ERROR_MESSAGE(log::Category::Script, LogSource, "Reload of '%s' failed, the previous version keeps running:\n%s",
                moduleName.c_str(), message.c_str());
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, message));
        };

        const auto code = ReadTextFile(module->path);
        if (!code)
            return fail("cannot read " + String(module->path.string().c_str()));
        if (auto loaded = state.Load(*code, ChunkName(module->path).c_str()); !loaded)
            return fail(loaded.error().message);

        // Run like require does: (name, file) as arguments, nil means true
        lua::Push(L, moduleName);
        lua::Push(L, module->path.generic_string());
        if (auto ran = state.ProtectedCall(2, 1); !ran)
            return fail(ran.error().message);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        const int fresh = lua_gettop(L);

        luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        const int loadedTable = lua_gettop(L);
        lua_getfield(L, loadedTable, moduleName.c_str());
        const int previousValue = lua_gettop(L);
        const bool isMain = moduleName == m_mainModuleName;
        bool startMain = false;
        String hookError;

        if (lua_istable(L, previousValue) && lua_istable(L, fresh)) {
            lua_getfield(L, LUA_REGISTRYINDEX, PatchKey);
            lua_pushvalue(L, previousValue);
            lua_pushvalue(L, fresh);
            if (auto patched = state.ProtectedCall(2, 1); !patched) {
                lua_settop(L, fresh - 1);
                return fail(patched.error().message);
            }
            RedirectUpvalues(L, previousValue, fresh);
            lua_pushliteral(L, "on_reload");
            if (lua_rawget(L, previousValue) == LUA_TFUNCTION) {
                lua_pushvalue(L, -2);
                if (auto hook = state.ProtectedCall(1, 0); !hook)
                    hookError = hook.error().message;
            } else {
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        } else {
            if (lua_istable(L, previousValue))
                WARNING_MESSAGE(log::Category::Script, LogSource,
                    "Module '%s' no longer returns a table: modules that required it keep the old value", moduleName.c_str());
            lua_pushvalue(L, fresh);
            lua_setfield(L, loadedTable, moduleName.c_str());
            if (isMain)
                startMain = true;
        }
        lua_settop(L, fresh - 1);

        module = FindLoaded(moduleName);
        module->error = hookError;
        ++module->reloadCount;

        if (!hookError.empty())
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s.on_reload failed:\n%s", moduleName.c_str(), hookError.c_str());
        LOG_MESSAGE(log::Category::Script, LogSource, "Reloaded '%s'", moduleName.c_str());

        if (m_onReloaded)
            m_onReloaded(moduleName, isMain, startMain, hookError);

        return { };
    }

    size_t ScriptModuleManager::ReloadChanged(lua::State& state)
    {
        Vector<String> changed;
        for (const auto& module : m_loadedModules) {
            if (WriteTimeOf(module.path) != module.writeTime)
                changed.push_back(module.name);
        }
        size_t reloaded = 0;
        for (const auto& name : changed) {
            if (ReloadModule(state, name))
                ++reloaded;
        }
        return reloaded;
    }

    void ScriptModuleManager::PollFileChanges(lua::State& state)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextPoll)
            return;
        m_nextPoll = now + PollInterval;

        Vector<String> ready;
        for (auto& module : m_loadedModules) {
            const int64_t writeTime = WriteTimeOf(module.path);
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
            (void)ReloadModule(state, name);
    }

    int ScriptModuleManager::SearchModule(lua_State* L)
    {
        auto* modules = ScriptContext::From(L).modules;
        ELM_ASSERT(modules);

        size_t nameLength = 0;
        const char* name = luaL_checklstring(L, 1, &nameLength);
        const StringView moduleName(name, nameLength);

        const Path path = modules->FindModule(moduleName);
        if (path.Empty()) {
            lua_pushfstring(L, IsValidModuleName(moduleName) ? "no module '%s' in the scripts folder" : "invalid module name '%s'", name);
            return 1;
        }

        LoadedModule* module = modules->FindLoaded(moduleName);
        if (!module)
            module = &modules->m_loadedModules.emplace_back(LoadedModule { String(moduleName), path, 0, 0, 0, {} });
        module->path = path;
        module->writeTime = WriteTimeOf(path);
        module->pendingWriteTime = 0;
        module->error.clear();

        const auto code = ReadTextFile(path);
        if (!code) {
            module->error = "cannot read the file";
            return luaL_error(L, "cannot read module '%s' from '%s'", name, path.string().c_str());
        }

        auto& state = lua::State::From(L);
        if (auto loaded = state.Load(*code, modules->ChunkName(path).c_str()); !loaded) {
            module->error = loaded.error().message;
            return luaL_error(L, "error loading module '%s':\n\t%s", name, loaded.error().message.c_str());
        }

        lua::Push(L, path.generic_string());
        return 2;
    }

    Vector<String> ScriptModuleManager::ListModules() const
    {
        Vector<String> modules;
        Vector<Path> folders { m_scriptRoot };
        while (!folders.empty()) {
            const Path folder = std::move(folders.back());
            folders.pop_back();
            std::error_code error;
            for (auto it = Directory(folder).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const Path path = it.CurrentPath();
                std::error_code typeError;
                if (it.IsDirectory(typeError)) {
                    if (!path.Filename().string().starts_with("."))
                        folders.push_back(path);
                } else if (String module = ModuleNameOf(path); !module.empty()) {
                    modules.push_back(std::move(module));
                }
            }
        }
        std::sort(modules.begin(), modules.end());
        return modules;
    }

} // namespace elm::script
