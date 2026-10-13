#pragma once

#include "core/Std.hpp"

#include "script/lua/Lua.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace elm::script::lua {

    /// Lua name of a class registered with Class<T>; empty while the class is not registered.
    template <typename T>
    struct UserType {
        static inline String name;
    };

    namespace detail {

        [[noreturn]] inline void TypeError(lua_State* L, int index, const char* expected)
        {
            luaL_typeerror(L, index, expected);
            std::unreachable();
        }

    } // namespace detail

    /// Classes registered with Class<T>: they live in full userdata as values, Check returns
    /// a reference into the userdata so a T& parameter edits the Lua object. A StackTraits
    /// specialization may derive from it to change one operation (e.g. cache pushed objects).
    template <typename T>
    struct UserTypeTraits {
        static_assert(std::is_class_v<T>,
            "No Lua conversion for this type: register the class with lua::Class<T> or specialize lua::StackTraits");
        static_assert(alignof(T) <= alignof(std::max_align_t) && alignof(T) <= 8,
            "Lua userdata is only aligned to 8 bytes");

        [[nodiscard]] static bool Registered() noexcept { return !UserType<T>::name.empty(); }

        template <typename U>
        static void Push(lua_State* L, U&& value)
        {
            if (!Registered())
                luaL_error(L, "C++ class %s is not registered in Lua", typeid(T).name());
            void* memory = lua_newuserdatauv(L, sizeof(T), 0);
            // Throws before the metatable is set: the userdata has no __gc and is just collected
            std::construct_at(static_cast<T*>(memory), std::forward<U>(value));
            luaL_setmetatable(L, UserType<T>::name.c_str());
        }

        [[nodiscard]] static T* Test(lua_State* L, int index) noexcept
        {
            return Registered() ? static_cast<T*>(luaL_testudata(L, index, UserType<T>::name.c_str())) : nullptr;
        }

        [[nodiscard]] static T& Check(lua_State* L, int index)
        {
            if (T* value = Test(L, index))
                return *value;
            detail::TypeError(L, index, Registered() ? UserType<T>::name.c_str() : typeid(T).name());
        }

        [[nodiscard]] static std::optional<T> Get(lua_State* L, int index)
        {
            if (const T* value = Test(L, index))
                return *value;
            return std::nullopt;
        }

        [[nodiscard]] static String TypeName() { return Registered() ? UserType<T>::name : String("any"); }
    };

    /// Conversion of a C++ type to and from the Lua stack:
    ///   Push(L, value)     pushes the value
    ///   Check(L, index)    reads an argument, raises a Lua error on a wrong type (only inside a Lua call)
    ///   Get(L, index)      reads without raising: std::nullopt on a wrong type
    ///   TypeName()         type name for the LuaLS annotations
    /// Types without a specialization are registered classes.
    template <typename T>
    struct StackTraits : UserTypeTraits<T> { };

    template <>
    struct StackTraits<bool> {
        static void Push(lua_State* L, bool value) { lua_pushboolean(L, value ? 1 : 0); }
        [[nodiscard]] static bool Check(lua_State* L, int index)
        {
            luaL_checktype(L, index, LUA_TBOOLEAN);
            return lua_toboolean(L, index) != 0;
        }
        [[nodiscard]] static std::optional<bool> Get(lua_State* L, int index)
        {
            if (!lua_isboolean(L, index))
                return std::nullopt;
            return lua_toboolean(L, index) != 0;
        }
        [[nodiscard]] static String TypeName() { return "boolean"; }
    };

    template <typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    struct StackTraits<T> {
        static void Push(lua_State* L, T value) { lua_pushinteger(L, static_cast<lua_Integer>(value)); }
        [[nodiscard]] static T Check(lua_State* L, int index)
        {
            const lua_Integer value = luaL_checkinteger(L, index);
            if (!std::in_range<T>(value))
                luaL_argerror(L, index, "integer out of range");
            return static_cast<T>(value);
        }
        [[nodiscard]] static std::optional<T> Get(lua_State* L, int index)
        {
            int isInteger = 0;
            const lua_Integer value = lua_tointegerx(L, index, &isInteger);
            if (!isInteger || !std::in_range<T>(value))
                return std::nullopt;
            return static_cast<T>(value);
        }
        [[nodiscard]] static String TypeName() { return "integer"; }
    };

    template <typename T>
        requires std::is_floating_point_v<T>
    struct StackTraits<T> {
        static void Push(lua_State* L, T value) { lua_pushnumber(L, static_cast<lua_Number>(value)); }
        [[nodiscard]] static T Check(lua_State* L, int index) { return static_cast<T>(luaL_checknumber(L, index)); }
        [[nodiscard]] static std::optional<T> Get(lua_State* L, int index)
        {
            int isNumber = 0;
            const lua_Number value = lua_tonumberx(L, index, &isNumber);
            if (!isNumber)
                return std::nullopt;
            return static_cast<T>(value);
        }
        [[nodiscard]] static String TypeName() { return "number"; }
    };

    /// Enums travel as their integer value.
    template <typename T>
        requires std::is_enum_v<T>
    struct StackTraits<T> {
        using Underlying = std::underlying_type_t<T>;
        static void Push(lua_State* L, T value) { StackTraits<Underlying>::Push(L, static_cast<Underlying>(value)); }
        [[nodiscard]] static T Check(lua_State* L, int index) { return static_cast<T>(StackTraits<Underlying>::Check(L, index)); }
        [[nodiscard]] static std::optional<T> Get(lua_State* L, int index)
        {
            const auto value = StackTraits<Underlying>::Get(L, index);
            return value ? std::optional<T>(static_cast<T>(*value)) : std::nullopt;
        }
        [[nodiscard]] static String TypeName() { return "integer"; }
    };

    namespace detail {

        /// Strings of any allocator; Get is strict (a number is not a string), Check converts like Lua does.
        template <typename S>
        struct StringTraits {
            static void Push(lua_State* L, const S& value) { lua_pushlstring(L, value.data(), value.size()); }
            [[nodiscard]] static S Check(lua_State* L, int index)
            {
                size_t length = 0;
                const char* text = luaL_checklstring(L, index, &length);
                return S(text, length);
            }
            [[nodiscard]] static std::optional<S> Get(lua_State* L, int index)
            {
                if (lua_type(L, index) != LUA_TSTRING)
                    return std::nullopt;
                size_t length = 0;
                const char* text = lua_tolstring(L, index, &length);
                return S(text, length);
            }
            [[nodiscard]] static String TypeName() { return "string"; }
        };

    } // namespace detail

    template <>
    struct StackTraits<String> : detail::StringTraits<String> { };
    template <>
    struct StackTraits<std::string> : detail::StringTraits<std::string> { };
    /// The view points into the Lua string: valid while the value stays on the stack (the call).
    template <>
    struct StackTraits<StringView> : detail::StringTraits<StringView> { };

    template <>
    struct StackTraits<const char*> {
        static void Push(lua_State* L, const char* value)
        {
            if (value)
                lua_pushstring(L, value);
            else
                lua_pushnil(L);
        }
        [[nodiscard]] static const char* Check(lua_State* L, int index) { return luaL_checkstring(L, index); }
        [[nodiscard]] static std::optional<const char*> Get(lua_State* L, int index)
        {
            if (lua_type(L, index) != LUA_TSTRING)
                return std::nullopt;
            return lua_tostring(L, index);
        }
        [[nodiscard]] static String TypeName() { return "string"; }
    };

    /// nil or missing <-> std::nullopt.
    template <typename T>
    struct StackTraits<std::optional<T>> {
        template <typename U>
        static void Push(lua_State* L, U&& value)
        {
            if (value)
                StackTraits<T>::Push(L, *std::forward<U>(value));
            else
                lua_pushnil(L);
        }
        [[nodiscard]] static std::optional<T> Check(lua_State* L, int index)
        {
            if (lua_isnoneornil(L, index))
                return std::nullopt;
            return std::optional<T>(StackTraits<T>::Check(L, index));
        }
        /// The outer optional is the conversion result, the inner one the value.
        [[nodiscard]] static std::optional<std::optional<T>> Get(lua_State* L, int index)
        {
            if (lua_isnoneornil(L, index))
                return std::optional<T>();
            auto value = StackTraits<T>::Get(L, index);
            if (!value)
                return std::nullopt;
            return std::optional<T>(std::move(*value));
        }
        [[nodiscard]] static String TypeName() { return StackTraits<T>::TypeName() + "?"; }
    };

    /// Decayed: a string literal is pushed as const char*.
    /// Arrays: a Lua sequence {v1, v2, ...}, elements converted one by one.
    template <typename T, typename A>
    struct StackTraits<std::vector<T, A>> {
        static void Push(lua_State* L, const std::vector<T, A>& values)
        {
            lua_createtable(L, static_cast<int>(values.size()), 0);
            for (size_t index = 0; index < values.size(); ++index) {
                StackTraits<T>::Push(L, values[index]);
                lua_rawseti(L, -2, static_cast<lua_Integer>(index + 1));
            }
        }
        [[nodiscard]] static std::vector<T, A> Check(lua_State* L, int index)
        {
            index = lua_absindex(L, index); // elements are pushed above it
            luaL_checktype(L, index, LUA_TTABLE);
            const auto count = static_cast<lua_Integer>(lua_rawlen(L, index));
            std::vector<T, A> values;
            values.reserve(static_cast<size_t>(count));
            for (lua_Integer element = 1; element <= count; ++element) {
                lua_rawgeti(L, index, element);
                values.push_back(StackTraits<T>::Check(L, -1));
                lua_pop(L, 1);
            }
            return values;
        }
        [[nodiscard]] static std::optional<std::vector<T, A>> Get(lua_State* L, int index)
        {
            index = lua_absindex(L, index);
            if (!lua_istable(L, index))
                return std::nullopt;
            const auto count = static_cast<lua_Integer>(lua_rawlen(L, index));
            std::vector<T, A> values;
            values.reserve(static_cast<size_t>(count));
            for (lua_Integer element = 1; element <= count; ++element) {
                lua_rawgeti(L, index, element);
                auto value = StackTraits<T>::Get(L, -1);
                lua_pop(L, 1);
                if (!value)
                    return std::nullopt;
                values.push_back(std::move(*value));
            }
            return values;
        }
        [[nodiscard]] static String TypeName() { return StackTraits<T>::TypeName() + "[]"; }
    };

    template <typename T>
    void Push(lua_State* L, T&& value)
    {
        StackTraits<std::decay_t<T>>::Push(L, std::forward<T>(value));
    }

    template <typename T>
    [[nodiscard]] String TypeNameOf()
    {
        return StackTraits<std::decay_t<T>>::TypeName();
    }

} // namespace elm::script::lua
