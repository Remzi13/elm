#pragma once

#include "script/values/Entity.hpp"

#include "script/lua/Ref.hpp"

#include "graphics/Model.hpp"

#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"

#include <optional>

namespace elm::script::bindings {

    /// A component type as entity:add/remove/has and elm.scene.with see it, by its Lua name
    /// (ComponentTypeName).
    struct ComponentKind {
        ComponentType type;
        /// The scene's store of the type, nullptr while the scene has none.
        const IComponentStore* (*find)(const Scene& scene);
        IComponentStore& (*store)(Scene& scene);
        /// Adds the component to a live node that has none; returns what entity:add gives back.
        lua::Ref (*add)(lua_State* L, NodeId node, const std::optional<lua::Ref>& init);
        /// Called before the component is removed (a Script instance gets on_destroy), may be null.
        void (*beforeRemove)(lua_State* L, NodeId node);
    };

    template <SceneComponent T>
    const IComponentStore* FindStore(const Scene& scene)
    {
        return scene.FindComponents<T>();
    }

    template <SceneComponent T>
    IComponentStore& StoreOf(Scene& scene)
    {
        return scene.Components<T>();
    }

    /// Lua handles of a node's RigidBody and Collider: their fields read and write the component
    /// in the scene, so a handle stays valid as long as the component is there.
    struct RigidBodyRef {
        NodeId node;
        bool operator==(const RigidBodyRef&) const = default;
    };
    struct ColliderRef {
        NodeId node;
        bool operator==(const ColliderRef&) const = default;
    };

    // PhysicsBindings.cpp
    [[nodiscard]] const ComponentKind& RigidBodyKind() noexcept;
    [[nodiscard]] const ComponentKind& ColliderKind() noexcept;
    /// entity.rigidbody, entity.collider: nil without the component.
    [[nodiscard]] std::optional<RigidBodyRef> GetRigidBody(lua_State* L, const Entity& entity);
    [[nodiscard]] std::optional<ColliderRef> GetCollider(lua_State* L, const Entity& entity);
    /// A collider around a model: a box of its bounds; sphere and capsule radius from the X/Z size.
    /// A flat model gets a thin box under it. The default collider without a model.
    [[nodiscard]] Collider FitColliderToModel(const graphics::Model* model);

} // namespace elm::script::bindings
