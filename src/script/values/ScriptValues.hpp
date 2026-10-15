#pragma once

#include "Scene/ScriptComponent.hpp"

#include "script/lua/Lua.hpp"

#include <optional>

namespace elm::script {

    /// Props of an entity script, sorted by name: the overrides of a ScriptComponent or the defaults of a class.
    using ScriptProps = Vector<std::pair<String, ScriptValue>>;

    /// The Lua value at index as a prop value; nullopt for a type props cannot hold.
    [[nodiscard]] std::optional<ScriptValue> ToScriptValue(lua_State* L, int index);
    /// Vectors are pushed as new objects, a node id as its Entity.
    void PushScriptValue(lua_State* L, const ScriptValue& value);
    /// "boolean", "number" (integer or not), "string", "Vector3", "Vector4" or "Entity".
    [[nodiscard]] const char* ScriptValueKind(const ScriptValue& value) noexcept;
    /// kind_of(value) -> the kind of a prop value, nil for a type props cannot hold.
    int ScriptValueKindOf(lua_State* L);

    /// Reads a `props` table of entity script values: string keys, values of the prop types.
    /// Raises a Lua error naming the offending prop otherwise.
    [[nodiscard]] ScriptProps CheckScriptProps(lua_State* L, int index);

} // namespace elm::script
