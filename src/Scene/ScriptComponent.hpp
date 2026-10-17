#pragma once

#include "core/Std.hpp"

#include "math/Vector.hpp"

#include "Scene/ComponentTypes.hpp"
#include "Scene/NodeId.hpp"

#include <variant>

namespace elm {

/// Value of an entity script prop: the types the scene can keep without Lua and the editor can show.
using ScriptValue = std::variant<bool, int64_t, double, String, math::Vector3, math::Vector4, NodeId>;

/// A Lua script on a scene node. The module returns a class table, script::EntityScripts gives every node
/// with this component an instance of it (see "Скрипты сущностей" in docs/script/Скрипты на Lua.md).
/// Only data lives here, so the component outlives Lua restarts: the instance is made again from it.
struct ScriptComponent {
    static constexpr ComponentType Type = ComponentType::Script;

    /// Module name as for require: "npc.guard" -> assets/scripts/npc/guard.lua.
    String module;
    /// Props that differ from the defaults of the module (its `props` table).
    Vector<std::pair<String, ScriptValue>> props;
};

} // namespace elm
