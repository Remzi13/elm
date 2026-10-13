#include "script/ScriptSystem.hpp"

#include "script/bindings/Bindings.hpp"

#include "core/AssetPaths.hpp"
#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>
#include <iterator>

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "ScriptSystem";
        constexpr auto PollInterval = std::chrono::milliseconds(250);

        // Registry keys of the Lua helpers installed by InstallHelpers
        constexpr const char* PatchKey = "elm.hot_patch";
        constexpr const char* FormatKey = "elm.console_format";

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

        /// format(...) -> text of the console results; tostring may run __tostring, hence a Lua call.
        constexpr const char* FormatSource = R"(
            return function(...)
                local parts = {}
                for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
                return table.concat(parts, "\t")
            end
        )";

        auto ReadTextFile(const Path& path) -> std::optional<String>
        {
            auto stream = FileSystem::OpenForReading(path, std::ios::in | std::ios::binary);
            if (!stream)
                return std::nullopt;
            String text { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
            // luaL_loadbuffer does not skip the UTF-8 byte order mark some editors write
            if (text.starts_with("\xEF\xBB\xBF"))
                text.erase(0, 3);
            return text;
        }

        int64_t WriteTimeOf(const Path& path)
        {
            std::error_code error;
            return FileSystem::LastWriteTime(path, error);
        }

        /// Module names are dotted identifiers: no path separators, no way out of the script root.
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

        bool IsNameCharacter(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        }

        /// String keys of the table at index that start with prefix.
        void CollectKeys(lua_State* L, int index, StringView prefix, Vector<String>& keys)
        {
            index = lua_absindex(L, index);
            lua_pushnil(L);
            while (lua_next(L, index)) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    const StringView key = lua_tostring(L, -2);
                    if (key.starts_with(prefix) && !key.starts_with("__"))
                        keys.emplace_back(key);
                }
                lua_pop(L, 1);
            }
        }

        /// Keys reachable from the value at index: a table's own keys and those of a table
        /// __index; for userdata of a lua::Class, its fields and methods (upvalues of __index).
        void CollectMembers(lua_State* L, int index, StringView prefix, bool methodsOnly, Vector<String>& keys)
        {
            index = lua_absindex(L, index);
            if (lua_istable(L, index))
                CollectKeys(L, index, prefix, keys);
            if (!lua_getmetatable(L, index))
                return;
            lua_pushliteral(L, "__index");
            const int indexType = lua_rawget(L, -2);
            if (indexType == LUA_TTABLE) {
                CollectKeys(L, -1, prefix, keys);
            } else if (indexType == LUA_TFUNCTION) {
                // lua::Class: upvalue 1 = field getters, upvalue 2 = class table with the methods
                for (int upvalue = methodsOnly ? 2 : 1; upvalue <= 2; ++upvalue) {
                    if (lua_getupvalue(L, -1, upvalue)) {
                        if (lua_istable(L, -1))
                            CollectKeys(L, -1, prefix, keys);
                        lua_pop(L, 1);
                    }
                }
            }
            lua_pop(L, 2);
        }

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

    ScriptSystem::~ScriptSystem()
    {
        Shutdown();
    }

    void ScriptSystem::RegisterSettings(Settings& settings)
    {
        settings.Register(Settings::Category::Script, EnabledSetting, "Run scripts", true);
        settings.Register(Settings::Category::Script, MainModuleSetting, "Main module", String("main"));
        settings.Register(Settings::Category::Script, HotReloadSetting, "Reload changed scripts", true);
    }

    auto ScriptSystem::Init(Path scriptRoot) -> EngineResult<void>
    {
        ELM_ASSERT_THREAD(Update);
        if (m_state)
            return { };

        m_scriptRoot = scriptRoot.Empty() ? AssetPaths::Scripts() : std::move(scriptRoot);
        m_state = MakeUnique<lua::State>();
        m_state->SetContext(this);
        m_state->OpenLibraries();
        InstallModuleSearcher();
        InstallHelpers();
        bindings::RegisterAll(*m_state);

        LOG_MESSAGE(log::Category::Script, LogSource, "%s initialized, scripts in %s", LUA_RELEASE,
            m_scriptRoot.string().c_str());
        return { };
    }

    auto ScriptSystem::Start(const Settings& settings) -> EngineResult<void>
    {
        if (!m_state)
            return MakeUnexpected(ErrorCode::ScriptError, "ScriptSystem::Start called before Init", log::Category::Script, LogSource);
        m_mainModuleName = settings.Get<String>(Settings::Category::Script, MainModuleSetting, String("main"));
        if (!settings.Get<bool>(Settings::Category::Script, EnabledSetting, true))
            return { };

        if (m_mainModuleName.empty() || FindModule(m_mainModuleName).Empty()) {
            LOG_MESSAGE(log::Category::Script, LogSource, "No main module '%s', scripts are idle", m_mainModuleName.c_str());
            return { };
        }

        lua_State* L = *m_state;
        if (auto required = Require(m_mainModuleName); !required)
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
            PollFileChanges();

        m_time.deltaTime = deltaTime;
        m_time.time += deltaTime;
        ++m_time.frame;
        CallHook("on_update", deltaTime, true);
    }

    void ScriptSystem::FixedUpdate(float fixedDeltaTime)
    {
        ELM_PROFILE_SCOPE_N("Scripts FixedUpdate");
        m_time.fixedDeltaTime = fixedDeltaTime;
        CallHook("on_fixed_update", fixedDeltaTime, true);
    }

    void ScriptSystem::Shutdown()
    {
        if (!m_state)
            return;
        ELM_ASSERT_THREAD(Update);

        CallHook("on_shutdown", 0.0f, false);
        // References go before the state they point into
        m_mainModule.Reset();
        m_state.reset();
        m_loadedModules.clear();
        m_mainFaulted = false;
        m_mainError.clear();
        m_restartRequested = false;
        m_time = { };
    }

    auto ScriptSystem::GetMainState() const noexcept -> MainState
    {
        if (m_mainFaulted)
            return MainState::Faulted;
        return m_mainModule ? MainState::Running : MainState::Idle;
    }

    auto ScriptSystem::Require(StringView name) -> EngineResult<void>
    {
        lua_State* L = *m_state;
        lua_getglobal(L, "require");
        lua::Push(L, name);
        auto result = m_state->ProtectedCall(1, 1);
        // An error while running the module body: keep it next to the module, a reload may fix it
        if (!result) {
            if (auto* module = FindLoaded(name); module && module->error.empty())
                module->error = result.error().message;
        }
        return result;
    }

    Path ScriptSystem::FindModule(StringView name) const
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

    String ScriptSystem::ModuleNameOf(const Path& file) const
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

    // ── Hot reload ──────────────────────────────────────────────────────────────

    auto ScriptSystem::ReloadModule(StringView name) -> EngineResult<void>
    {
        if (!m_state)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        LoadedModule* module = FindLoaded(name);
        if (!module)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "module '" + String(name) + "' is not loaded"));

        lua_State* L = *m_state;
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
        if (auto loaded = m_state->Load(*code, ChunkName(module->path).c_str()); !loaded)
            return fail(loaded.error().message);

        // Run like require does: (name, file) as arguments, nil means true
        lua::Push(L, moduleName);
        lua::Push(L, module->path.generic_string());
        if (auto ran = m_state->ProtectedCall(2, 1); !ran)
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
            if (auto patched = m_state->ProtectedCall(2, 1); !patched) {
                lua_settop(L, fresh - 1);
                return fail(patched.error().message);
            }
            RedirectUpvalues(L, previousValue, fresh);
            // stack: fresh, loaded, old, previous fields
            lua_pushliteral(L, "on_reload");
            if (lua_rawget(L, previousValue) == LUA_TFUNCTION) {
                lua_pushvalue(L, -2);
                if (auto hook = m_state->ProtectedCall(1, 0); !hook)
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
            if (isMain) {
                // A main module that failed to load until now starts with this version
                startMain = !m_mainModule;
                m_mainModule = lua::Ref(L, fresh);
            }
        }
        lua_settop(L, fresh - 1);

        module = FindLoaded(moduleName);
        module->error = hookError;
        ++module->reloadCount;
        if (isMain) {
            m_mainFaulted = false;
            m_mainError.clear();
        }
        if (!hookError.empty())
            ERROR_MESSAGE(log::Category::Script, LogSource, "%s.on_reload failed:\n%s", moduleName.c_str(), hookError.c_str());
        LOG_MESSAGE(log::Category::Script, LogSource, "Reloaded '%s'", moduleName.c_str());

        if (startMain)
            CallHook("on_start", 0.0f, false);
        return { };
    }

    size_t ScriptSystem::ReloadChanged()
    {
        Vector<String> changed;
        for (const auto& module : m_loadedModules) {
            if (WriteTimeOf(module.path) != module.writeTime)
                changed.push_back(module.name);
        }
        size_t reloaded = 0;
        for (const auto& name : changed) {
            if (ReloadModule(name))
                ++reloaded;
        }
        return reloaded;
    }

    void ScriptSystem::PollFileChanges()
    {
        if (!m_state)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now < m_nextPoll)
            return;
        m_nextPoll = now + PollInterval;

        // A change is applied once two polls in a row see the same file time: editors save in steps
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
            (void)ReloadModule(name); // failures are logged and kept on the module
    }

    auto ScriptSystem::Restart() -> EngineResult<void>
    {
        m_restartRequested = false;
        const Path root = m_scriptRoot;
        Shutdown();
        if (auto init = Init(root); !init)
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
        return !m_engine.settings || m_engine.settings->Get<bool>(Settings::Category::Script, HotReloadSetting, true);
    }

    auto ScriptSystem::FindLoaded(StringView name) -> LoadedModule*
    {
        const auto it = std::find_if(m_loadedModules.begin(), m_loadedModules.end(),
            [&](const LoadedModule& module) { return module.name == name; });
        return it == m_loadedModules.end() ? nullptr : &*it;
    }

    // ── Console ─────────────────────────────────────────────────────────────────

    auto ScriptSystem::Execute(StringView code) -> EngineResult<String>
    {
        if (!m_state)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        lua_State* L = *m_state;
        const int top = lua_gettop(L);

        // "1 + 2" is an expression, "x = 1" a statement: try the first, then the second
        const String expression = "return " + String(code);
        if (!m_state->Load(expression, "=console")) {
            if (auto loaded = m_state->Load(code, "=console"); !loaded)
                return MakeUnexpected(loaded.error());
        }

        String output;
        String* outer = std::exchange(m_consoleCapture, &output);
        auto ran = m_state->ProtectedCall(0, LUA_MULTRET);
        if (ran) {
            const int resultCount = lua_gettop(L) - top;
            if (resultCount > 0) {
                lua_getfield(L, LUA_REGISTRYINDEX, FormatKey);
                lua_insert(L, top + 1);
                ran = m_state->ProtectedCall(resultCount, 1);
                if (ran) {
                    size_t length = 0;
                    const char* text = lua_tolstring(L, -1, &length);
                    output.append(text, length);
                }
            }
        }
        m_consoleCapture = outer;
        lua_settop(L, top);

        if (!ran)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, output + ran.error().message));
        if (output.ends_with('\n'))
            output.pop_back();
        return output;
    }

    Vector<String> ScriptSystem::Complete(StringView text)
    {
        Vector<String> completions;
        if (!m_state)
            return completions;

        // The dotted name at the end of the text: "x = elm.scene.sp" -> "elm.scene.sp"
        size_t start = text.size();
        while (start > 0 && (IsNameCharacter(text[start - 1]) || text[start - 1] == '.' || text[start - 1] == ':'))
            --start;
        const StringView path = text.substr(start);
        const size_t separator = path.find_last_of(".:");
        const StringView partial = separator == StringView::npos ? path : path.substr(separator + 1);
        const bool methodsOnly = separator != StringView::npos && path[separator] == ':';

        lua_State* L = *m_state;
        lua::StackGuard guard(L);
        lua_pushglobaltable(L);
        if (separator != StringView::npos) {
            // Walk the prefix with raw access only: completion must not run script code
            const StringView owner = path.substr(0, separator);
            size_t begin = 0;
            while (begin <= owner.size()) {
                const size_t end = (std::min)(owner.find_first_of(".:", begin), owner.size());
                const String segment(owner.substr(begin, end - begin));
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    return completions;
                }
                lua_pushstring(L, segment.c_str());
                lua_rawget(L, -2);
                lua_remove(L, -2);
                begin = end + 1;
            }
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                return completions;
            }
        }

        Vector<String> names;
        CollectMembers(L, -1, partial, methodsOnly, names);
        lua_pop(L, 1);

        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        const String head(text.substr(0, text.size() - partial.size()));
        for (const auto& name : names)
            completions.push_back(head + name);
        return completions;
    }

    // ── Loading ─────────────────────────────────────────────────────────────────

    String ScriptSystem::ChunkName(const Path& path) const
    {
        // "@relative/path.lua": errors and tracebacks show the file relative to the script root
        return "@" + String(path.LexicallyRelative(m_scriptRoot).generic_string().c_str());
    }

    void ScriptSystem::InstallModuleSearcher()
    {
        // package.searchers = { preload, engine file system }: no package.path lookups, no native modules
        lua_State* L = *m_state;
        lua_getglobal(L, LUA_LOADLIBNAME);
        lua_getfield(L, -1, "searchers");
        lua_rawgeti(L, -1, 1);
        lua_newtable(L);
        lua_insert(L, -2);
        lua_rawseti(L, -2, 1);
        lua_pushcfunction(L, &ScriptSystem::SearchModule);
        lua_rawseti(L, -2, 2);
        lua_setfield(L, -3, "searchers");
        lua_pop(L, 2);
    }

    void ScriptSystem::InstallHelpers()
    {
        lua_State* L = *m_state;
        for (const auto& [key, source] : { std::pair { PatchKey, PatchSource }, std::pair { FormatKey, FormatSource } }) {
            const bool installed = m_state->Load(source, "=engine") && m_state->ProtectedCall(0, 1);
            ELM_ASSERT(installed);
            if (installed)
                lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    }

    int ScriptSystem::SearchModule(lua_State* L)
    {
        auto& system = From(L);
        size_t nameLength = 0;
        const char* name = luaL_checklstring(L, 1, &nameLength);
        const StringView moduleName(name, nameLength);

        const Path path = system.FindModule(moduleName);
        if (path.Empty()) {
            lua_pushfstring(L, IsValidModuleName(moduleName) ? "no module '%s' in the scripts folder" : "invalid module name '%s'", name);
            return 1;
        }

        // Listed before it compiles: a module with a syntax error is watched too and loads once fixed
        LoadedModule* module = system.FindLoaded(moduleName);
        if (!module)
            module = &system.m_loadedModules.emplace_back(LoadedModule { String(moduleName), path });
        module->path = path;
        module->writeTime = WriteTimeOf(path);
        module->pendingWriteTime = 0;
        module->error.clear();

        const auto code = ReadTextFile(path);
        if (!code) {
            module->error = "cannot read the file";
            return luaL_error(L, "cannot read module '%s' from '%s'", name, path.string().c_str());
        }
        if (auto loaded = system.m_state->Load(*code, system.ChunkName(path).c_str()); !loaded) {
            module->error = loaded.error().message;
            return luaL_error(L, "error loading module '%s':\n\t%s", name, loaded.error().message.c_str());
        }

        lua::Push(L, path.generic_string());
        return 2;
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
        // Raw access: a metamethod could raise an error outside a protected call
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
                m_mainModuleName.c_str(), name, called.error().message.c_str());
        }
    }

} // namespace elm::script
