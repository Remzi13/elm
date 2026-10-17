#include "script/bindings/Bindings.hpp"
#include "script/bindings/Components.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "graphics/ModelManager.hpp"
#include "physics/PhysicsSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace elm::script::bindings {

    namespace {

        using math::Vector3;

        constexpr double Unbounded = std::numeric_limits<double>::infinity();

        /// What elm.physics and the RigidBody and Collider handles reach.
        struct PhysicsContext {
            PhysicsSystem* physics { nullptr };
            Scene* scene { nullptr };
            graphics::ModelManager* models { nullptr };
        };

        PhysicsContext& Context(lua_State* L) { return ContextOf<PhysicsContext>(L); }
        Scene& SceneOf(lua_State* L) { return RequireSystem(L, Context(L).scene, "elm.scene"); }
        PhysicsSystem& PhysicsOf(lua_State* L) { return RequireSystem(L, Context(L).physics, "elm.physics"); }

        void CheckAlive(lua_State* L, NodeId node)
        {
            if (SceneOf(L).IndexOf(node) == Scene::NoNode)
                luaL_error(L, "the entity was destroyed");
        }

        float CheckRange(lua_State* L, float value, const char* field, double min, double max)
        {
            if (std::isfinite(value) && value >= min && value <= max)
                return value;
            // lua_pushfstring knows %f (as %.14g), not %g
            const auto number = static_cast<lua_Number>(value);
            if (min == -Unbounded && max == Unbounded)
                luaL_error(L, "%s must be a finite number, got %f", field, number);
            else if (max == Unbounded)
                luaL_error(L, "%s must be at least %f, got %f", field, min, number);
            else
                luaL_error(L, "%s must be in %f..%f, got %f", field, min, max, number);
            return value;
        }

        Vector3 CheckSize(lua_State* L, const Vector3& value, const char* field)
        {
            return { CheckRange(L, value.x, field, 0.0, Unbounded), CheckRange(L, value.y, field, 0.0, Unbounded),
                CheckRange(L, value.z, field, 0.0, Unbounded) };
        }

        Vector3 CheckFinite(lua_State* L, const Vector3& value, const char* field)
        {
            return { CheckRange(L, value.x, field, -Unbounded, Unbounded), CheckRange(L, value.y, field, -Unbounded, Unbounded),
                CheckRange(L, value.z, field, -Unbounded, Unbounded) };
        }

        // ── Names ───────────────────────────────────────────────────────────────

        BodyType CheckBodyType(lua_State* L, StringView name)
        {
            if (const auto type = BodyTypeFromName(name))
                return *type;
            luaL_error(L, "unknown body type '%s' (static, kinematic, dynamic)", String(name).c_str());
            std::unreachable();
        }

        ColliderShape CheckShape(lua_State* L, StringView name)
        {
            if (const auto shape = ColliderShapeFromName(name))
                return *shape;
            luaL_error(L, "unknown collider shape '%s' (box, sphere, capsule)", String(name).c_str());
            std::unreachable();
        }

        // ── RigidBody ───────────────────────────────────────────────────────────

        RigidBody& RigidBodyOf(lua_State* L, const RigidBodyRef& ref)
        {
            CheckAlive(L, ref.node);
            RigidBody* body = SceneOf(L).Components<RigidBody>().Find(ref.node);
            if (!body)
                luaL_error(L, "the RigidBody was removed");
            return *body;
        }

        bool IsRigidBodyValid(lua_State* L, const RigidBodyRef& ref)
        {
            const Scene* scene = Context(L).scene;
            const auto* bodies = scene ? scene->FindComponents<RigidBody>() : nullptr;
            return bodies && scene->IndexOf(ref.node) != Scene::NoNode && bodies->Contains(ref.node);
        }

        /// The node's body, made now when the components were added since the last physics step.
        PhysicsSystem& BodyOf(lua_State* L, NodeId node)
        {
            auto& physics = PhysicsOf(L);
            if (!physics.SyncNode(SceneOf(L), node))
                luaL_error(L, "the entity has no physics body: it needs a Collider");
            return physics;
        }

        /// Velocities and forces: a kinematic body moves with its node, a static one not at all.
        PhysicsSystem& DynamicBodyOf(lua_State* L, const RigidBodyRef& ref, const char* what)
        {
            const BodyType type = RigidBodyOf(L, ref).type;
            if (type != BodyType::Dynamic)
                luaL_error(L, "%s needs a dynamic body, this one is %s", what, BodyTypeName(type));
            return BodyOf(L, ref.node);
        }

        Vector3 GetVelocity(lua_State* L, const RigidBodyRef& ref)
        {
            RigidBodyOf(L, ref);
            return BodyOf(L, ref.node).GetLinearVelocity(ref.node).value_or(Vector3 { });
        }
        void SetVelocity(lua_State* L, RigidBodyRef& ref, const Vector3& velocity)
        {
            DynamicBodyOf(L, ref, "velocity").SetLinearVelocity(ref.node, CheckFinite(L, velocity, "velocity"));
        }
        Vector3 GetAngularVelocity(lua_State* L, const RigidBodyRef& ref)
        {
            RigidBodyOf(L, ref);
            return BodyOf(L, ref.node).GetAngularVelocity(ref.node).value_or(Vector3 { });
        }
        void SetAngularVelocity(lua_State* L, RigidBodyRef& ref, const Vector3& velocity)
        {
            DynamicBodyOf(L, ref, "angular_velocity").SetAngularVelocity(ref.node, CheckFinite(L, velocity, "angular_velocity"));
        }
        /// Acts during the next physics step.
        void AddForce(lua_State* L, RigidBodyRef& ref, const Vector3& force)
        {
            DynamicBodyOf(L, ref, "add_force").AddForce(ref.node, CheckFinite(L, force, "force"));
        }
        /// Changes the velocity at once by impulse / mass.
        void AddImpulse(lua_State* L, RigidBodyRef& ref, const Vector3& impulse)
        {
            DynamicBodyOf(L, ref, "add_impulse").AddImpulse(ref.node, CheckFinite(L, impulse, "impulse"));
        }

        String RigidBodyToString(lua_State* L, const RigidBodyRef& ref)
        {
            if (!IsRigidBodyValid(L, ref))
                return "RigidBody(removed)";
            return String("RigidBody(") + BodyTypeName(RigidBodyOf(L, ref).type) + ", " + SceneOf(L).Find(ref.node)->name + ")";
        }

        // ── Collider ────────────────────────────────────────────────────────────

        Collider& ColliderOf(lua_State* L, const ColliderRef& ref)
        {
            CheckAlive(L, ref.node);
            Collider* collider = SceneOf(L).Components<Collider>().Find(ref.node);
            if (!collider)
                luaL_error(L, "the Collider was removed");
            return *collider;
        }

        bool IsColliderValid(lua_State* L, const ColliderRef& ref)
        {
            const Scene* scene = Context(L).scene;
            const auto* colliders = scene ? scene->FindComponents<Collider>() : nullptr;
            return colliders && scene->IndexOf(ref.node) != Scene::NoNode && colliders->Contains(ref.node);
        }

        String ColliderToString(lua_State* L, const ColliderRef& ref)
        {
            if (!IsColliderValid(L, ref))
                return "Collider(removed)";
            const Collider& collider = ColliderOf(L, ref);
            return String("Collider(") + ColliderShapeName(collider.shape) + (collider.trigger ? " trigger, " : ", ") + SceneOf(L).Find(ref.node)->name
                + ")";
        }

        /// The collider an entity gets before init applies: around the entity's model.
        Collider FitToModel(lua_State* L, NodeId node)
        {
            const auto* models = Context(L).models;
            const auto* nodeModels = SceneOf(L).FindComponents<ModelComponent>();
            const ModelComponent* component = nodeModels ? nodeModels->Find(node) : nullptr;
            return FitColliderToModel(models && component ? models->Get(component->model) : nullptr);
        }

        // ── entity:add ──────────────────────────────────────────────────────────

        /// handle[key] = value for every field of init: the handle's setters check the values.
        int AssignFields(lua_State* L)
        {
            lua_pushnil(L);
            while (lua_next(L, 2)) {
                lua_pushvalue(L, -2);
                lua_insert(L, -2);
                lua_settable(L, 1);
            }
            return 0;
        }

        /// The handle of a component just added, with init's fields written into it. On a bad
        /// field the component goes away again and the error is raised.
        template <typename Handle>
        lua::Ref FinishAdd(lua_State* L, const Handle& handle, const std::optional<lua::Ref>& init, IComponentStore& (*store)(Scene&))
        {
            if (init && init->Type() != LUA_TTABLE) {
                store(SceneOf(L)).Remove(handle.node);
                luaL_error(L, "add: init must be a table of fields");
            }
            lua::Push(L, handle);
            if (init) {
                lua_pushcfunction(L, &AssignFields);
                lua_pushvalue(L, -2);
                init->Push(L);
                if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                    store(SceneOf(L)).Remove(handle.node);
                    lua_error(L);
                }
            }
            lua::Ref result(L, -1);
            lua_pop(L, 1);
            return result;
        }

        lua::Ref AddRigidBody(lua_State* L, NodeId node, const std::optional<lua::Ref>& init)
        {
            SceneOf(L).Components<RigidBody>().Emplace(node);
            return FinishAdd(L, RigidBodyRef { node }, init, &StoreOf<RigidBody>);
        }

        lua::Ref AddCollider(lua_State* L, NodeId node, const std::optional<lua::Ref>& init)
        {
            SceneOf(L).Components<Collider>().Emplace(node, FitToModel(L, node));
            return FinishAdd(L, ColliderRef { node }, init, &StoreOf<Collider>);
        }

        // The physics system drops a body on its next step once the component is gone
        constexpr ComponentKind RigidBodyComponent { ComponentType::RigidBody, &FindStore<RigidBody>, &StoreOf<RigidBody>, &AddRigidBody, nullptr };
        constexpr ComponentKind ColliderComponent { ComponentType::Collider, &FindStore<Collider>, &StoreOf<Collider>, &AddCollider, nullptr };

        // ── elm.physics ─────────────────────────────────────────────────────────

        /// raycast(origin, direction, max_distance?, ignore?) -> entity, point, normal, distance of the
        /// closest solid body (triggers are skipped), nothing on a miss. ignore: an entity the ray starts in.
        std::tuple<std::optional<Entity>, std::optional<Vector3>, std::optional<Vector3>, std::optional<float>> Raycast(lua_State* L,
            const Vector3& origin, const Vector3& direction, std::optional<float> maxDistance, std::optional<Entity> ignore)
        {
            const auto hit = PhysicsOf(L).CastRay(origin, direction, maxDistance.value_or(1000.0f), ignore ? ignore->id : NodeId { });
            if (!hit)
                return { };
            return { Entity { hit->node }, hit->point, hit->normal, hit->distance };
        }

    } // namespace

    Collider FitColliderToModel(const graphics::Model* model)
    {
        Collider collider;
        if (!model || model->submeshes.empty())
            return collider;
        const auto bounds = model->GetLocalBounds();
        if (bounds.minBounds.x > bounds.maxBounds.x)
            return collider;

        Vector3 half = (bounds.maxBounds - bounds.minBounds) * 0.5f;
        Vector3 center = (bounds.maxBounds + bounds.minBounds) * 0.5f;
        constexpr float FlatThickness = 0.25f;
        for (float Vector3::*axis : { &Vector3::x, &Vector3::y, &Vector3::z }) {
            if (half.*axis < 0.01f) {
                half.*axis = FlatThickness;
                center.*axis -= FlatThickness;
            }
        }
        collider.halfExtents = half;
        collider.offset = center;
        collider.radius = std::max(half.x, half.z);
        collider.halfHeight = std::max(half.y - collider.radius, 0.0f);
        return collider;
    }

    const ComponentKind& RigidBodyKind() noexcept { return RigidBodyComponent; }
    const ComponentKind& ColliderKind() noexcept { return ColliderComponent; }

    std::optional<RigidBodyRef> GetRigidBody(lua_State* L, const Entity& entity)
    {
        CheckAlive(L, entity.id);
        const auto* bodies = SceneOf(L).FindComponents<RigidBody>();
        if (!bodies || !bodies->Contains(entity.id))
            return std::nullopt;
        return RigidBodyRef { entity.id };
    }

    std::optional<ColliderRef> GetCollider(lua_State* L, const Entity& entity)
    {
        CheckAlive(L, entity.id);
        const auto* colliders = SceneOf(L).FindComponents<Collider>();
        if (!colliders || !colliders->Contains(entity.id))
            return std::nullopt;
        return ColliderRef { entity.id };
    }

    void RegisterPhysics(lua::State& state, PhysicsSystem* physics, Scene* scene, graphics::ModelManager* models)
    {
        state.SetContext(PhysicsContext { physics, scene, models });
        lua::Module module(state, "elm.physics");
        module
            .Function<&Raycast>("raycast", { "origin", "direction", "max_distance", "ignore" })
            .Function<[](lua_State* L) { return PhysicsOf(L).GetGravity(); }>("gravity")
            .Function<[](lua_State* L, const Vector3& gravity) { PhysicsOf(L).SetGravity(CheckFinite(L, gravity, "gravity")); }>(
                "set_gravity", { "gravity" })
            .Function<[](lua_State* L) { return PhysicsOf(L).GetNumBodies(); }>("body_count");

        lua::Class<RigidBodyRef>(module, "RigidBody")
            .Property<&IsRigidBodyValid>("valid")
            .Property<[](const RigidBodyRef& ref) { return Entity { ref.node }; }>("entity")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return String(BodyTypeName(RigidBodyOf(L, ref).type)); },
                [](lua_State* L, RigidBodyRef& ref, StringView type) { RigidBodyOf(L, ref).type = CheckBodyType(L, type); }>("type")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).mass; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).mass = CheckRange(L, value, "mass", 0.001, Unbounded); }>("mass")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).friction; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).friction = CheckRange(L, value, "friction", 0.0, Unbounded); }>("friction")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).restitution; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).restitution = CheckRange(L, value, "restitution", 0.0, 1.0); }>("restitution")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).linearDamping; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).linearDamping = CheckRange(L, value, "linear_damping", 0.0, 1.0); }>("linear_damping")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).angularDamping; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).angularDamping = CheckRange(L, value, "angular_damping", 0.0, 1.0); }>("angular_damping")
            .Property<[](lua_State* L, const RigidBodyRef& ref) { return RigidBodyOf(L, ref).gravityScale; },
                [](lua_State* L, RigidBodyRef& ref, float value) { RigidBodyOf(L, ref).gravityScale = CheckRange(L, value, "gravity_scale", -Unbounded, Unbounded); }>("gravity_scale")
            .Property<&GetVelocity, &SetVelocity>("velocity")
            .Property<&GetAngularVelocity, &SetAngularVelocity>("angular_velocity")
            .Method<&AddForce>("add_force", { "force" })
            .Method<&AddImpulse>("add_impulse", { "impulse" })
            .Meta<&RigidBodyToString>("__tostring");

        lua::Class<ColliderRef>(module, "Collider")
            .Property<&IsColliderValid>("valid")
            .Property<[](const ColliderRef& ref) { return Entity { ref.node }; }>("entity")
            .Property<[](lua_State* L, const ColliderRef& ref) { return String(ColliderShapeName(ColliderOf(L, ref).shape)); },
                [](lua_State* L, ColliderRef& ref, StringView shape) { ColliderOf(L, ref).shape = CheckShape(L, shape); }>("shape")
            .Property<[](lua_State* L, const ColliderRef& ref) { return ColliderOf(L, ref).halfExtents; },
                [](lua_State* L, ColliderRef& ref, const Vector3& value) { ColliderOf(L, ref).halfExtents = CheckSize(L, value, "half_extents"); }>("half_extents")
            .Property<[](lua_State* L, const ColliderRef& ref) { return ColliderOf(L, ref).radius; },
                [](lua_State* L, ColliderRef& ref, float value) { ColliderOf(L, ref).radius = CheckRange(L, value, "radius", 0.0, Unbounded); }>("radius")
            .Property<[](lua_State* L, const ColliderRef& ref) { return ColliderOf(L, ref).halfHeight; },
                [](lua_State* L, ColliderRef& ref, float value) { ColliderOf(L, ref).halfHeight = CheckRange(L, value, "half_height", 0.0, Unbounded); }>("half_height")
            .Property<[](lua_State* L, const ColliderRef& ref) { return ColliderOf(L, ref).offset; },
                [](lua_State* L, ColliderRef& ref, const Vector3& value) { ColliderOf(L, ref).offset = CheckFinite(L, value, "offset"); }>("offset")
            .Property<[](lua_State* L, const ColliderRef& ref) { return ColliderOf(L, ref).trigger; },
                [](lua_State* L, ColliderRef& ref, bool value) { ColliderOf(L, ref).trigger = value; }>("trigger")
            .Meta<&ColliderToString>("__tostring");
    }

} // namespace elm::script::bindings
