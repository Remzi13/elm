#include "script/bindings/Bindings.hpp"

#include "core/Log.hpp"

#include <cstring>

namespace elm::script::bindings {

    namespace {

        /// File and line of the Lua code that called the current C function.
        struct CallerLocation {
            String file { "?" };
            int line { 0 };
        };

        /// The nearest Lua function up the stack: print may be called through C (pcall, the console).
        auto GetCallerLocation(lua_State* L) -> CallerLocation
        {
            CallerLocation location;
            lua_Debug debug;
            for (int level = 1; lua_getstack(L, level, &debug); ++level) {
                if (lua_getinfo(L, "Sl", &debug) && std::strcmp(debug.what, "C") != 0) {
                    location.file = debug.short_src;
                    location.line = debug.currentline;
                    break;
                }
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
            return 0;
        }

    } // namespace

    void RegisterLog(lua::State& state)
    {
        lua_pushcfunction(state, &Print);
        lua_setglobal(state, "print");

        lua::Module(state, "elm.log")
            .Function<&LogInfo>("info", { "message" })
            .Function<&LogWarning>("warn", { "message" })
            .Function<&LogError>("error", { "message" });
    }

} // namespace elm::script::bindings
