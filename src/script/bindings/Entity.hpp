#pragma once

#include "Scene/TestScenes.hpp"

#include "script/lua/Stack.hpp"

namespace elm::script::bindings {

    /// A scene node as scripts see it: only the stable NodeId, so it stays safe to hold
    /// after the node is removed (every access but `valid` then raises an error).
    struct Entity {
        NodeId id;
        bool operator==(const Entity&) const = default;
    };

    /// A model of the scene's ModelManager.
    struct ModelRef {
        graphics::ModelHandler handle;
        bool operator==(const ModelRef&) const = default;
    };

    /// Registry table: raw NodeId -> its userdata, weak values.
    inline constexpr const char* EntityCacheKey = "elm.entity_cache";

} // namespace elm::script::bindings

namespace elm::script::lua {

    /// One userdata per node: the same node always pushes the same object, so entities are
    /// rawequal and work as table keys.
    template <>
    struct StackTraits<bindings::Entity> : UserTypeTraits<bindings::Entity> {
        static void Push(lua_State* L, const bindings::Entity& entity)
        {
            if (luaL_getsubtable(L, LUA_REGISTRYINDEX, bindings::EntityCacheKey) == 0) {
                lua_createtable(L, 0, 1);
                lua_pushliteral(L, "v");
                lua_setfield(L, -2, "__mode");
                lua_setmetatable(L, -2);
            }
            const auto key = static_cast<lua_Integer>(entity.id.Raw());
            if (lua_rawgeti(L, -1, key) == LUA_TNIL) {
                lua_pop(L, 1);
                UserTypeTraits<bindings::Entity>::Push(L, entity);
                lua_pushvalue(L, -1);
                lua_rawseti(L, -3, key);
            }
            lua_remove(L, -2);
        }
    };

} // namespace elm::script::lua
