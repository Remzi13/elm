#include "script/bindings/ScriptValues.hpp"

#include "script/bindings/Entity.hpp"

#include <algorithm>

namespace elm::script::bindings {

    namespace {

        constexpr const char* PropTypes = "boolean, number, string, Vector3, Vector4, Entity";

    } // namespace

    std::optional<ScriptValue> ToScriptValue(lua_State* L, int index)
    {
        switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            return ScriptValue(lua_toboolean(L, index) != 0);
        case LUA_TNUMBER:
            if (lua_isinteger(L, index))
                return ScriptValue(static_cast<int64_t>(lua_tointeger(L, index)));
            return ScriptValue(static_cast<double>(lua_tonumber(L, index)));
        case LUA_TSTRING:
            return ScriptValue(*lua::StackTraits<String>::Get(L, index));
        case LUA_TUSERDATA:
            if (const auto* vector = lua::StackTraits<math::Vector3>::Test(L, index))
                return ScriptValue(*vector);
            if (const auto* vector = lua::StackTraits<math::Vector4>::Test(L, index))
                return ScriptValue(*vector);
            if (const auto* entity = lua::StackTraits<Entity>::Test(L, index))
                return ScriptValue(entity->id);
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    void PushScriptValue(lua_State* L, const ScriptValue& value)
    {
        std::visit([L]<typename V>(const V& item) {
            if constexpr (std::is_same_v<V, NodeId>)
                lua::Push(L, Entity { item });
            else
                lua::Push(L, item);
        }, value);
    }

    const char* ScriptValueKind(const ScriptValue& value) noexcept
    {
        return std::visit([]<typename V>(const V&) -> const char* {
            if constexpr (std::is_same_v<V, bool>)
                return "boolean";
            else if constexpr (std::is_same_v<V, int64_t> || std::is_same_v<V, double>)
                return "number";
            else if constexpr (std::is_same_v<V, String>)
                return "string";
            else if constexpr (std::is_same_v<V, math::Vector3>)
                return "Vector3";
            else if constexpr (std::is_same_v<V, math::Vector4>)
                return "Vector4";
            else
                return "Entity";
        }, value);
    }

    int ScriptValueKindOf(lua_State* L)
    {
        const auto value = ToScriptValue(L, 1);
        if (value)
            lua_pushstring(L, ScriptValueKind(*value));
        else
            lua_pushnil(L);
        return 1;
    }

    Vector<std::pair<String, ScriptValue>> CheckScriptProps(lua_State* L, int index)
    {
        index = lua_absindex(L, index);
        luaL_checktype(L, index, LUA_TTABLE);
        Vector<std::pair<String, ScriptValue>> props;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                luaL_error(L, "props: keys must be strings, got %s", luaL_typename(L, -2));
            String name(*lua::StackTraits<String>::Get(L, -2));
            if (name == "entity")
                luaL_error(L, "props.entity: the name is taken by self.entity");
            auto value = ToScriptValue(L, -1);
            if (!value)
                luaL_error(L, "props.%s: %s cannot be a prop (%s)", name.c_str(), luaL_typename(L, -1), PropTypes);
            props.emplace_back(std::move(name), std::move(*value));
            lua_pop(L, 1);
        }
        // lua_next order is arbitrary: keep the props in a stable order
        std::sort(props.begin(), props.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
        return props;
    }

} // namespace elm::script::bindings
