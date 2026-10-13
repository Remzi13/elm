#pragma once

#include "core/Debug.hpp"

#include "script/lua/Function.hpp"
#include "script/lua/State.hpp"

namespace elm::script::lua {

    /// Registers functions and values in a module table: a global path ("elm.math" is
    /// elm.math) that require also returns. Keeps the table on the stack while it lives,
    /// so builders must be destroyed in reverse order (plain scopes do that).
    ///
    ///     lua::Module(state, "elm.log")
    ///         .Function<&Info>("info", { "message" })
    ///         .Value("version", 1);
    class Module {
    public:
        Module(State& state, StringView path)
            : m_state(state)
            , m_path(path)
            , m_api(state.Api().AddModule(path))
        {
            lua_State* L = state;
            m_base = lua_gettop(L);

            lua_pushglobaltable(L);
            size_t begin = 0;
            while (begin <= m_path.size()) {
                const size_t end = (std::min)(m_path.find('.', begin), m_path.size());
                const String segment = m_path.substr(begin, end - begin);
                if (lua_getfield(L, -1, segment.c_str()) != LUA_TTABLE) {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    lua_pushvalue(L, -1);
                    lua_setfield(L, -3, segment.c_str());
                }
                lua_remove(L, -2);
                begin = end + 1;
            }

            luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, m_path.c_str());
            lua_pop(L, 1);
        }

        ~Module()
        {
            ELM_ASSERT(lua_gettop(m_state) == m_base + 1);
            lua_settop(m_state, m_base);
        }

        Module(const Module&) = delete;
        Module& operator=(const Module&) = delete;

        /// Fn: a function pointer, a captureless lambda or a raw int(lua_State*) function.
        template <auto Fn>
        Module& Function(const char* name, ParamNames names = { })
        {
            lua_pushcfunction(m_state, detail::CFunctionOf<Fn>());
            lua_setfield(m_state, Index(), name);
            m_state.Api().ModuleAt(m_api).functions.push_back(
                detail::Describe<Fn>(name, ApiFunction::Kind::Function, names, false));
            return *this;
        }

        template <typename V>
        Module& Value(const char* name, V&& value)
        {
            lua::Push(m_state, std::forward<V>(value));
            lua_setfield(m_state, Index(), name);
            ApiField field;
            field.name = name;
            field.type = &TypeNameOf<V>;
            m_state.Api().ModuleAt(m_api).values.push_back(std::move(field));
            return *this;
        }

        [[nodiscard]] State& GetState() const noexcept { return m_state; }
        /// Stack index of the module table.
        [[nodiscard]] int Index() const noexcept { return m_base + 1; }
        [[nodiscard]] const String& Path() const noexcept { return m_path; }

    private:
        State& m_state;
        String m_path;
        size_t m_api;
        int m_base { 0 };
    };

} // namespace elm::script::lua
