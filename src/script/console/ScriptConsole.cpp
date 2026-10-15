#include "script/console/ScriptConsole.hpp"

#include "script/lua/State.hpp"

#include "core/Debug.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>
#include <utility>

namespace elm::script {

    namespace {

        /// Registry key of the result formatter.
        constexpr const char* FormatKey = "elm.console_format";

        /// format(...) -> text of the console results; tostring may run __tostring, hence a Lua call.
        constexpr const char* FormatSource = R"(
            return function(...)
                local parts = {}
                for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
                return table.concat(parts, "\t")
            end
        )";

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

        /// Pushes the value the dotted owner path ("elm.scene") leads to, walking with raw access only:
        /// completion must not run script code. False, nothing pushed, when the path leads nowhere.
        bool PushOwner(lua_State* L, StringView owner)
        {
            lua_pushglobaltable(L);
            size_t begin = 0;
            while (begin <= owner.size()) {
                const size_t end = (std::min)(owner.find_first_of(".:", begin), owner.size());
                const String segment(owner.substr(begin, end - begin));
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    return false;
                }
                lua_pushstring(L, segment.c_str());
                lua_rawget(L, -2);
                lua_remove(L, -2);
                begin = end + 1;
            }
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                return false;
            }
            return true;
        }

    } // namespace

    ScriptConsole::ScriptConsole(lua::State& state) noexcept
        : m_state(state)
    {
    }

    void ScriptConsole::Init()
    {
        const bool installed = m_state.InstallHelper(FormatKey, FormatSource).has_value();
        ELM_ASSERT(installed && "the console helper is engine code and must load");

        // print = the console's print around the one the bindings registered
        lua_State* L = m_state;
        lua_pushglobaltable(L);
        lua_pushlightuserdata(L, this);
        lua_pushliteral(L, "print");
        lua_rawget(L, -3);
        lua_pushcclosure(L, &ScriptConsole::Print, 2);
        lua_pushliteral(L, "print");
        lua_insert(L, -2);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }

    auto ScriptConsole::Execute(StringView code) -> EngineResult<String>
    {
        if (!m_state.IsOpen())
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, "scripts are not initialized"));
        lua_State* L = m_state;
        const int top = lua_gettop(L);

        // "1 + 2" is an expression, "x = 1" a statement: try the first, then the second
        const String expression = "return " + String(code);
        if (!m_state.Load(expression, "=console")) {
            if (auto loaded = m_state.Load(code, "=console"); !loaded)
                return MakeUnexpected(loaded.error());
        }

        String output;
        String* outer = std::exchange(m_output, &output);
        auto ran = Run(output);
        m_output = outer;
        lua_settop(L, top);

        if (!ran)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, output + ran.error().message));
        if (output.ends_with('\n'))
            output.pop_back();
        return output;
    }

    auto ScriptConsole::Run(String& output) -> EngineResult<void>
    {
        lua_State* L = m_state;
        const int top = lua_gettop(L) - 1;
        if (auto ran = m_state.ProtectedCall(0, LUA_MULTRET); !ran)
            return ran;
        const int resultCount = lua_gettop(L) - top;
        if (resultCount == 0)
            return { };
        lua_getfield(L, LUA_REGISTRYINDEX, FormatKey);
        lua_insert(L, top + 1);
        if (auto formatted = m_state.ProtectedCall(resultCount, 1); !formatted)
            return formatted;
        size_t length = 0;
        const char* text = lua_tolstring(L, -1, &length);
        output.append(text, length);
        return { };
    }

    Vector<String> ScriptConsole::Complete(StringView text)
    {
        Vector<String> completions;
        if (!m_state.IsOpen())
            return completions;

        // The dotted name at the end of the text: "x = elm.scene.sp" -> "elm.scene.sp"
        size_t start = text.size();
        while (start > 0 && (IsNameCharacter(text[start - 1]) || text[start - 1] == '.' || text[start - 1] == ':'))
            --start;
        const StringView path = text.substr(start);
        const size_t separator = path.find_last_of(".:");
        const StringView partial = separator == StringView::npos ? path : path.substr(separator + 1);
        const bool methodsOnly = separator != StringView::npos && path[separator] == ':';

        lua_State* L = m_state;
        lua::StackGuard guard(L);
        if (separator == StringView::npos)
            lua_pushglobaltable(L);
        else if (!PushOwner(L, path.substr(0, separator)))
            return completions;

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

    int ScriptConsole::Print(lua_State* L)
    {
        const auto& console = *static_cast<const ScriptConsole*>(lua_touserdata(L, lua_upvalueindex(1)));
        String line;
        const int count = lua_gettop(L);
        for (int index = 1; index <= count; ++index) {
            size_t length = 0;
            const char* text = luaL_tolstring(L, index, &length);
            if (index > 1)
                line += '\t';
            line.append(text, length);
            lua_pop(L, 1);
        }
        // A function that kept print outside a command only logs
        if (console.m_output) {
            *console.m_output += line;
            *console.m_output += '\n';
        }
        lua_pushvalue(L, lua_upvalueindex(2));
        lua_pushlstring(L, line.data(), line.size());
        lua_call(L, 1, 0);
        return 0;
    }

} // namespace elm::script
