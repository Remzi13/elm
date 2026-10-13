#pragma once

#include "core/Error.hpp"
#include "core/Unexpected.hpp"

#include "script/lua/Stack.hpp"
#include "script/lua/State.hpp"

#include <utility>

namespace elm::script::lua {

    /// Keeps a Lua value alive from C++ (a slot in the registry). Move-only.
    /// Every Ref has to be released before its State is destroyed.
    class Ref {
    public:
        Ref() noexcept = default;
        /// References the value at index, the value stays on the stack.
        Ref(lua_State* L, int index)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
            // The main thread outlives every coroutine the value could have come from
            m_state = lua_tothread(L, -1);
            lua_pop(L, 1);
            lua_pushvalue(L, index);
            m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        ~Ref() { Reset(); }

        Ref(const Ref&) = delete;
        Ref& operator=(const Ref&) = delete;
        Ref(Ref&& other) noexcept
            : m_state(std::exchange(other.m_state, nullptr))
            , m_ref(std::exchange(other.m_ref, LUA_NOREF))
        {
        }
        Ref& operator=(Ref&& other) noexcept
        {
            if (this != &other) {
                Reset();
                m_state = std::exchange(other.m_state, nullptr);
                m_ref = std::exchange(other.m_ref, LUA_NOREF);
            }
            return *this;
        }

        /// A second reference to the same value.
        [[nodiscard]] Ref Copy() const
        {
            if (!m_state)
                return { };
            Push(m_state);
            Ref copy(m_state, -1);
            lua_pop(m_state, 1);
            return copy;
        }

        void Reset() noexcept
        {
            if (m_state)
                luaL_unref(m_state, LUA_REGISTRYINDEX, m_ref);
            m_state = nullptr;
            m_ref = LUA_NOREF;
        }

        /// Refers to a value other than nil.
        [[nodiscard]] bool IsValid() const noexcept { return m_state && m_ref != LUA_NOREF && m_ref != LUA_REFNIL; }
        explicit operator bool() const noexcept { return IsValid(); }

        /// Pushes the value (nil for an empty Ref) onto L, a thread of the same state.
        void Push(lua_State* L) const
        {
            if (m_state)
                lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
            else
                lua_pushnil(L);
        }

        /// Lua type of the value (LUA_TNIL for an empty Ref).
        [[nodiscard]] int Type() const
        {
            if (!m_state)
                return LUA_TNIL;
            const int type = lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_ref);
            lua_pop(m_state, 1);
            return type;
        }

        [[nodiscard]] lua_State* GetState() const noexcept { return m_state; }

    protected:
        lua_State* m_state { nullptr };
        int m_ref { LUA_NOREF };
    };

    /// Reference to a Lua function that C++ calls later (callbacks, script hooks).
    class FunctionRef : public Ref {
    public:
        using Ref::Ref;

        /// Calls the function in protected mode. R is void or a type with StackTraits;
        /// a script error, or a result of another type, comes back as ErrorCode::ScriptError.
        template <typename R = void, typename... Args>
        auto Call(Args&&... args) const -> EngineResult<R>
        {
            if (!IsValid())
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "call through an empty function reference"));

            lua_State* L = m_state;
            const int top = lua_gettop(L);
            if (!lua_checkstack(L, static_cast<int>(sizeof...(Args)) + 2))
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "Lua stack overflow"));
            Push(L);
            (lua::Push(L, std::forward<Args>(args)), ...);
            constexpr int resultCount = std::is_void_v<R> ? 0 : 1;
            if (auto called = State::From(L).ProtectedCall(static_cast<int>(sizeof...(Args)), resultCount); !called)
                return MakeUnexpected(called.error());

            if constexpr (std::is_void_v<R>) {
                return { };
            } else {
                auto value = StackTraits<R>::Get(L, -1);
                const String resultType = luaL_typename(L, -1);
                lua_settop(L, top);
                if (!value)
                    return MakeUnexpected(EngineError(ErrorCode::ScriptError,
                        "function returned " + resultType + ", " + TypeNameOf<R>() + " expected"));
                return std::move(*value);
            }
        }
    };

    /// Any value, nil included.
    template <>
    struct StackTraits<Ref> {
        static void Push(lua_State* L, const Ref& value) { value.Push(L); }
        [[nodiscard]] static Ref Check(lua_State* L, int index) { return Ref(L, index); }
        [[nodiscard]] static std::optional<Ref> Get(lua_State* L, int index) { return Ref(L, index); }
        [[nodiscard]] static String TypeName() { return "any"; }
    };

    template <>
    struct StackTraits<FunctionRef> {
        static void Push(lua_State* L, const FunctionRef& value) { value.Push(L); }
        [[nodiscard]] static FunctionRef Check(lua_State* L, int index)
        {
            luaL_checktype(L, index, LUA_TFUNCTION);
            return FunctionRef(L, index);
        }
        [[nodiscard]] static std::optional<FunctionRef> Get(lua_State* L, int index)
        {
            if (!lua_isfunction(L, index))
                return std::nullopt;
            return FunctionRef(L, index);
        }
        [[nodiscard]] static String TypeName() { return "function"; }
    };

} // namespace elm::script::lua
