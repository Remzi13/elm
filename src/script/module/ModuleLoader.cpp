#include "script/module/ModuleLoader.hpp"

#include "script/lua/Stack.hpp"
#include "script/lua/State.hpp"
#include "script/module/ModulePaths.hpp"

#include "core/Unexpected.hpp"

#include <algorithm>
#include <iterator>

namespace elm::script {

    ModuleLoader::ModuleLoader(lua::State& state, const ModulePaths& paths) noexcept
        : m_state(state)
        , m_paths(paths)
    {
    }

    void ModuleLoader::Init()
    {
        // package.searchers = { preload, the engine file system }
        lua_State* L = m_state;
        lua_getglobal(L, LUA_LOADLIBNAME);
        lua_getfield(L, -1, "searchers");
        lua_rawgeti(L, -1, 1);
        lua_newtable(L);
        lua_insert(L, -2);
        lua_rawseti(L, -2, 1);
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, &ModuleLoader::Search, 1);
        lua_rawseti(L, -2, 2);
        lua_setfield(L, -3, "searchers");
        lua_pop(L, 2);
    }

    void ModuleLoader::Shutdown() noexcept
    {
        m_modules.clear();
    }

    auto ModuleLoader::Require(StringView name) -> EngineResult<void>
    {
        if (!m_state.IsOpen())
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        lua_State* L = m_state;
        lua_getglobal(L, "require");
        lua::Push(L, name);
        auto result = m_state.ProtectedCall(1, 1);
        // An error while running the module body: keep it next to the module, a reload may fix it
        if (!result) {
            if (auto* module = Find(name); module && module->error.empty())
                module->error = result.error().message;
        }
        return result;
    }

    auto ModuleLoader::LoadChunk(const Path& file) -> EngineResult<void>
    {
        auto stream = FileSystem::OpenForReading(file, std::ios::in | std::ios::binary);
        if (!stream)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "cannot read " + String(file.string().c_str())));
        String code { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        // luaL_loadbuffer does not skip the UTF-8 byte order mark some editors write
        if (code.starts_with("\xEF\xBB\xBF"))
            code.erase(0, 3);
        return m_state.Load(code, m_paths.ChunkName(file).c_str());
    }

    LoadedModule* ModuleLoader::Find(StringView name) noexcept
    {
        const auto it = std::find_if(m_modules.begin(), m_modules.end(), [&](const LoadedModule& module) { return module.name == name; });
        return it == m_modules.end() ? nullptr : &*it;
    }

    LoadedModule& ModuleLoader::Track(StringView name, const Path& file)
    {
        LoadedModule* module = Find(name);
        if (!module)
            module = &m_modules.emplace_back(LoadedModule { String(name), file });
        module->path = file;
        module->writeTime = ModulePaths::WriteTimeOf(file);
        module->pendingWriteTime = 0;
        module->error.clear();
        return *module;
    }

    int ModuleLoader::Search(lua_State* L)
    {
        auto& loader = *static_cast<ModuleLoader*>(lua_touserdata(L, lua_upvalueindex(1)));
        size_t nameLength = 0;
        const char* name = luaL_checklstring(L, 1, &nameLength);
        const StringView moduleName(name, nameLength);

        const Path file = loader.m_paths.Find(moduleName);
        if (file.Empty()) {
            lua_pushfstring(L, ModulePaths::IsValidName(moduleName) ? "no module '%s' in the scripts folder" : "invalid module name '%s'", name);
            return 1;
        }

        // Listed before it compiles: a module with a syntax error is watched too and loads once fixed
        LoadedModule& module = loader.Track(moduleName, file);
        if (auto loaded = loader.LoadChunk(file); !loaded) {
            module.error = loaded.error().message;
            return luaL_error(L, "error loading module '%s':\n\t%s", name, loaded.error().message.c_str());
        }
        lua::Push(L, file.generic_string());
        return 2;
    }

} // namespace elm::script
