#include "script/bindings/Bindings.hpp"
#include "script/ScriptSystem.hpp"

#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Settings.hpp"

#include <variant>

namespace elm::script::bindings {

    namespace {

        // ── elm.log, print ──────────────────────────────────────────────────────

        /// File and line of the Lua code that called the current C function.
        struct CallerLocation {
            String file { "?" };
            int line { 0 };
        };

        auto GetCallerLocation(lua_State* L) -> CallerLocation
        {
            CallerLocation location;
            lua_Debug debug;
            if (lua_getstack(L, 1, &debug) && lua_getinfo(L, "Sl", &debug)) {
                location.file = debug.short_src;
                location.line = debug.currentline;
            }
            return location;
        }

        void LogFromScript(lua_State* L, log::Severity severity, StringView message)
        {
            const auto location = GetCallerLocation(L);
            const String text(message);
            log::add(severity, log::Category::Script, "lua", location.file.c_str(), location.line, "%s", text.c_str());
        }

        void LogInfo(lua_State* L, StringView message) { LogFromScript(L, log::Severity::Info, message); }
        void LogWarning(lua_State* L, StringView message) { LogFromScript(L, log::Severity::Warning, message); }
        void LogError(lua_State* L, StringView message) { LogFromScript(L, log::Severity::Error, message); }

        /// print(...) goes to the engine log, arguments joined by tabs as in standard Lua.
        int Print(lua_State* L)
        {
            String message;
            const int count = lua_gettop(L);
            for (int index = 1; index <= count; ++index) {
                size_t length = 0;
                const char* text = luaL_tolstring(L, index, &length);
                if (index > 1)
                    message += '\t';
                message.append(text, length);
                lua_pop(L, 1);
            }
            LogInfo(L, message);
            // A console command shows its own output
            if (String* capture = ScriptSystem::From(L).ConsoleCapture()) {
                *capture += message;
                *capture += '\n';
            }
            return 0;
        }

        // ── elm.time ────────────────────────────────────────────────────────────

        float Delta(lua_State* L) { return ScriptSystem::From(L).Time().deltaTime; }
        float FixedDelta(lua_State* L) { return ScriptSystem::From(L).Time().fixedDeltaTime; }
        double Now(lua_State* L) { return ScriptSystem::From(L).Time().time; }
        uint64_t Frame(lua_State* L) { return ScriptSystem::From(L).Time().frame; }

        // ── elm.script ──────────────────────────────────────────────────────────

        EngineResult<void> Reload(lua_State* L, StringView name) { return ScriptSystem::From(L).ReloadModule(name); }
        size_t ReloadChanged(lua_State* L) { return ScriptSystem::From(L).ReloadChanged(); }
        void Restart(lua_State* L) { ScriptSystem::From(L).RequestRestart(); }

        Vector<String> Modules(lua_State* L)
        {
            Vector<String> names;
            for (const auto& module : ScriptSystem::From(L).LoadedModules())
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

        // ── elm.settings ────────────────────────────────────────────────────────

        const char* CategoryName(Settings::Category category)
        {
            switch (category) {
            case Settings::Category::Core: return "Core";
            case Settings::Category::Render: return "Render";
            case Settings::Category::Physics: return "Physics";
            case Settings::Category::Script: return "Script";
            }
            return "?";
        }

        /// Entry by its full key, "Category/path" as in the .settings file.
        auto FindSetting(const Settings& settings, StringView key) -> std::optional<Settings::Entry>
        {
            for (auto& entry : settings.GetEntries()) {
                const String entryKey = String(CategoryName(entry.category)) + "/" + entry.path;
                if (entryKey == key)
                    return std::move(entry);
            }
            return std::nullopt;
        }

        /// elm.settings.get(key) -> value or nil
        int GetSetting(lua_State* L)
        {
            const auto& settings = RequireSystem(L, Engine(L).settings, "elm.settings");
            const auto entry = FindSetting(settings, luaL_checkstring(L, 1));
            if (!entry) {
                lua_pushnil(L);
                return 1;
            }
            std::visit([L](const auto& value) { lua::Push(L, value); }, entry->value);
            return 1;
        }

        /// elm.settings.set(key, value): the value keeps the type the setting was registered with.
        int SetSetting(lua_State* L)
        {
            auto& settings = RequireSystem(L, Engine(L).settings, "elm.settings");
            const char* key = luaL_checkstring(L, 1);
            const auto entry = FindSetting(settings, key);
            if (!entry)
                return luaL_error(L, "unknown setting '%s'", key);

            const StringView path(entry->path);
            std::visit([&](const auto& current) {
                using V = std::decay_t<decltype(current)>;
                settings.Set(entry->category, path, lua::StackTraits<V>::Check(L, 2));
            }, entry->value);
            return 0;
        }

    } // namespace

    void RegisterCore(lua::State& state)
    {
        lua_pushcfunction(state, &Print);
        lua_setglobal(state, "print");

        lua::Module(state, "elm.log")
            .Function<&LogInfo>("info", { "message" })
            .Function<&LogWarning>("warn", { "message" })
            .Function<&LogError>("error", { "message" });

        lua::Module(state, "elm.time")
            .Function<&Delta>("delta")
            .Function<&FixedDelta>("fixed_delta")
            .Function<&Now>("now")
            .Function<&Frame>("frame");

        lua::Module(state, "elm.script")
            .Function<&Reload>("reload", { "module" })
            .Function<&ReloadChanged>("reload_changed")
            .Function<&Restart>("restart")
            .Function<&Modules>("modules")
            .Function<&Persist>("persist", { "key", "default" })
            .Function<&LuaMemory>("memory")
            .Function<&EngineMemory>("engine_memory");

        lua::Module(state, "elm.settings")
            .Function<&GetSetting>("get", { "key" })
            .Function<&SetSetting>("set", { "key", "value" });
    }

    void RegisterAll(lua::State& state)
    {
        RegisterCore(state);
        RegisterMath(state);
        RegisterInput(state);
        RegisterScene(state);
        RegisterPhysics(state);
    }

} // namespace elm::script::bindings
