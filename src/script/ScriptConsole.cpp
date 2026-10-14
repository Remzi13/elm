#include "script/ScriptConsole.hpp"

#include "core/Debug.hpp"
#include "core/Unexpected.hpp"

#include <algorithm>
#include <utility>

namespace elm::script {

    namespace {

        constexpr const char* FormatKey = "elm.console_format";

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

    } // namespace

    void ScriptConsole::InstallHelpers(lua::State& state)
    {
        lua_State* L = state.Get();
        const bool installed = state.Load(FormatSource, "=engine") && state.ProtectedCall(0, 1);
        ELM_ASSERT(installed);
        if (installed)
            lua_setfield(L, LUA_REGISTRYINDEX, FormatKey);
    }

    auto ScriptConsole::Execute(lua::State& state, StringView code) -> EngineResult<String>
    {
        lua_State* L = state.Get();
        const int top = lua_gettop(L);

        // "1 + 2" is an expression, "x = 1" a statement: try the first, then the second
        const String expression = "return " + String(code);
        if (!state.Load(expression, "=console")) {
            if (auto loaded = state.Load(code, "=console"); !loaded)
                return MakeUnexpected(loaded.error());
        }

        String output;
        String* outer = std::exchange(m_capture, &output);
        auto ran = state.ProtectedCall(0, LUA_MULTRET);
        if (ran) {
            const int resultCount = lua_gettop(L) - top;
            if (resultCount > 0) {
                lua_getfield(L, LUA_REGISTRYINDEX, FormatKey);
                lua_insert(L, top + 1);
                ran = state.ProtectedCall(resultCount, 1);
                if (ran) {
                    size_t length = 0;
                    const char* text = lua_tolstring(L, -1, &length);
                    output.append(text, length);
                }
            }
        }
        m_capture = outer;
        lua_settop(L, top);

        if (!ran)
            return MakeUnexpected(EngineError(ErrorCode::ScriptError, output + ran.error().message));
        if (output.ends_with('\n'))
            output.pop_back();
        return output;
    }

    Vector<String> ScriptConsole::Complete(lua::State& state, StringView text)
    {
        Vector<String> completions;
        lua_State* L = state.Get();

        // The dotted name at the end of the text: "x = elm.scene.sp" -> "elm.scene.sp"
        size_t start = text.size();
        while (start > 0 && (IsNameCharacter(text[start - 1]) || text[start - 1] == '.' || text[start - 1] == ':'))
            --start;
        const StringView path = text.substr(start);
        const size_t separator = path.find_last_of(".:");
        const StringView partial = separator == StringView::npos ? path : path.substr(separator + 1);
        const bool methodsOnly = separator != StringView::npos && path[separator] == ':';

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

} // namespace elm::script
