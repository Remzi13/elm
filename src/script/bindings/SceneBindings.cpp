#include "script/bindings/Bindings.hpp"
#include "script/bindings/Components.hpp"

#include "script/entity/EntityScripts.hpp"
#include "script/values/Entity.hpp"
#include "script/values/ScriptValues.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/ScriptComponent.hpp"
#include "graphics/ModelManager.hpp"
#include "math/Rotation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

namespace elm::script::bindings {

    namespace {

        using math::Matrix4x4;
        using math::Quaternion;
        using math::Vector3;
        using math::Vector4;

        /// What elm.scene, the Entity class and the Script component reach.
        struct SceneContext {
            Scene* scene { nullptr };
            graphics::ModelManager* models { nullptr };
            EntityScripts* entityScripts { nullptr };
        };

        SceneContext& Context(lua_State* L) { return ContextOf<SceneContext>(L); }
        Scene& SceneOf(lua_State* L) { return RequireSystem(L, Context(L).scene, "elm.scene"); }
        graphics::ModelManager& ModelsOf(lua_State* L) { return RequireSystem(L, Context(L).models, "elm.models"); }
        EntityScripts& ScriptsOf(lua_State* L) { return RequireSystem(L, Context(L).entityScripts, "entity scripts"); }

        // ── Entity ──────────────────────────────────────────────────────────────

        size_t IndexOf(lua_State* L, const Entity& entity)
        {
            const size_t index = SceneOf(L).IndexOf(entity.id);
            if (index == Scene::NoNode)
                luaL_error(L, "the entity was destroyed");
            return index;
        }

        const Scene::Node& NodeOf(lua_State* L, const Entity& entity) { return SceneOf(L).Nodes()[IndexOf(L, entity)]; }

        /// The id of a live entity, for the scene's setters; a destroyed one raises the error.
        NodeId AliveId(lua_State* L, const Entity& entity)
        {
            IndexOf(L, entity);
            return entity.id;
        }

        Entity EntityAt(const Scene& scene, size_t index) { return { scene.IdOf(index) }; }

        bool IsValid(lua_State* L, const Entity& entity)
        {
            const Scene* scene = Context(L).scene;
            return scene && scene->IndexOf(entity.id) != Scene::NoNode;
        }

        String GetName(lua_State* L, const Entity& entity) { return NodeOf(L, entity).name; }
        void SetName(lua_State* L, Entity& entity, String name) { SceneOf(L).SetName(AliveId(L, entity), std::move(name)); }

        // Model, color and visible are the entity's ModelComponent. An entity without one reads its defaults;
        // a write of color or visible gives it one, so they work without a model as on an entity with one.

        ModelComponent ModelOrDefault(lua_State* L, const Entity& entity)
        {
            IndexOf(L, entity);
            const auto* models = SceneOf(L).FindComponents<ModelComponent>();
            const ModelComponent* model = models ? models->Find(entity.id) : nullptr;
            return model ? *model : ModelComponent { };
        }

        ModelComponent& ModelToWrite(lua_State* L, const Entity& entity)
        {
            const NodeId id = AliveId(L, entity);
            auto& models = SceneOf(L).Components<ModelComponent>();
            ModelComponent* model = models.Find(id);
            return model ? *model : models.Emplace(id);
        }

        bool GetVisible(lua_State* L, const Entity& entity) { return ModelOrDefault(L, entity).visible; }
        void SetVisible(lua_State* L, Entity& entity, bool visible) { ModelToWrite(L, entity).visible = visible; }
        Vector4 GetColor(lua_State* L, const Entity& entity) { return ModelOrDefault(L, entity).color; }
        void SetColor(lua_State* L, Entity& entity, const Vector4& color) { ModelToWrite(L, entity).color = color; }

        // Local transform, relative to the parent. Writes mark the scene dirty, world
        // transforms follow before culling (or on the next world_* read).

        Matrix4x4 GetLocal(lua_State* L, const Entity& entity) { return NodeOf(L, entity).localTransform; }
        void SetLocal(lua_State* L, Entity& entity, const Matrix4x4& transform)
        {
            SceneOf(L).SetLocalTransform(AliveId(L, entity), transform);
        }

        Vector3 GetPosition(lua_State* L, const Entity& entity)
        {
            const auto& m = NodeOf(L, entity).localTransform;
            return { m(0, 3), m(1, 3), m(2, 3) };
        }
        void SetPosition(lua_State* L, Entity& entity, const Vector3& position)
        {
            Matrix4x4 m = GetLocal(L, entity);
            m(0, 3) = position.x;
            m(1, 3) = position.y;
            m(2, 3) = position.z;
            SetLocal(L, entity, m);
        }

        Quaternion GetRotation(lua_State* L, const Entity& entity) { return math::DecomposeTRS(GetLocal(L, entity)).rotation; }
        void SetRotation(lua_State* L, Entity& entity, const Quaternion& rotation)
        {
            const auto trs = math::DecomposeTRS(GetLocal(L, entity));
            SetLocal(L, entity, math::ComposeTRS(trs.translation, math::Normalize(rotation), trs.scale));
        }

        Vector3 GetScale(lua_State* L, const Entity& entity) { return math::DecomposeTRS(GetLocal(L, entity)).scale; }
        void SetScale(lua_State* L, Entity& entity, const Vector3& scale)
        {
            const auto trs = math::DecomposeTRS(GetLocal(L, entity));
            SetLocal(L, entity, math::ComposeTRS(trs.translation, trs.rotation, scale));
        }

        void Translate(lua_State* L, Entity& entity, const Vector3& offset)
        {
            SetPosition(L, entity, GetPosition(L, entity) + offset);
        }

        /// Turns the entity around an axis of its parent's space.
        void Rotate(lua_State* L, Entity& entity, const Vector3& axis, float radians)
        {
            SetRotation(L, entity, math::Multiply(math::QuaternionFromAxisAngle(axis, radians), GetRotation(L, entity)));
        }

        // World transform

        Matrix4x4 GetWorld(lua_State* L, const Entity& entity)
        {
            auto& scene = SceneOf(L);
            scene.UpdateWorldTransforms();
            return scene.WorldTransforms()[IndexOf(L, entity)];
        }
        void SetWorld(lua_State* L, Entity& entity, const Matrix4x4& transform)
        {
            if (!SceneOf(L).SetWorldTransform(AliveId(L, entity), transform))
                luaL_error(L, "cannot place the entity: its parent transform is singular");
        }
        Vector3 GetWorldPosition(lua_State* L, const Entity& entity)
        {
            const Matrix4x4 m = GetWorld(L, entity);
            return { m(0, 3), m(1, 3), m(2, 3) };
        }

        // Hierarchy

        std::optional<Entity> GetParent(lua_State* L, const Entity& entity)
        {
            const NodeId parent = NodeOf(L, entity).parent;
            if (!parent)
                return std::nullopt;
            return Entity { parent };
        }

        /// Reparents and keeps the world transform. nil detaches.
        void SetParent(lua_State* L, Entity& entity, std::optional<Entity> parent)
        {
            const NodeId node = AliveId(L, entity);
            const NodeId parentNode = parent ? AliveId(L, *parent) : NodeId { };
            switch (SceneOf(L).SetParent(node, parentNode)) {
            case Scene::SetParentResult::Done:
                return;
            case Scene::SetParentResult::StaleNode:
                luaL_error(L, "the entity was destroyed");
                return;
            case Scene::SetParentResult::Cycle:
                luaL_error(L, "an entity cannot become a child of itself or of its descendant");
                return;
            case Scene::SetParentResult::SingularParent:
                luaL_error(L, "cannot attach to a parent with a singular transform");
                return;
            }
        }

        Vector<Entity> Children(lua_State* L, const Entity& entity)
        {
            const NodeId id = AliveId(L, entity);
            Vector<Entity> children;
            for (const auto& node : SceneOf(L).Nodes()) {
                if (node.parent == id)
                    children.push_back({ node.id });
            }
            return children;
        }

        /// Removes the entity with all its descendants.
        void Destroy(lua_State* L, Entity& entity)
        {
            auto& scene = SceneOf(L);
            const size_t root = IndexOf(L, entity);
            const auto nodes = scene.Nodes();
            // Descendants come after the entity, each after its parent: one pass finds them all
            Vector<bool> doomed(nodes.size(), false);
            doomed[root] = true;
            Vector<NodeId> doomedIds { entity.id };
            for (size_t index = root + 1; index < nodes.size(); ++index) {
                const size_t parent = scene.IndexOf(nodes[index].parent);
                if (parent == Scene::NoNode || !doomed[parent])
                    continue;
                doomed[index] = true;
                doomedIds.push_back(nodes[index].id);
            }

            // Scripts get on_destroy while their entities still exist. It may change the scene,
            // ids of nodes it removed already just find nothing.
            ScriptsOf(L).Destroy(doomedIds);
            for (const NodeId id : doomedIds)
                scene.RemoveNode(id);
        }

        std::optional<ModelRef> GetModel(lua_State* L, const Entity& entity)
        {
            const auto handle = ModelOrDefault(L, entity).model;
            if (!handle)
                return std::nullopt;
            return ModelRef { handle };
        }
        /// nil takes the model away and keeps the color and visible flag for a model set later.
        void SetModel(lua_State* L, Entity& entity, std::optional<ModelRef> model)
        {
            if (model && !ModelsOf(L).Get(model->handle))
                luaL_error(L, "the model was removed");
            auto& models = SceneOf(L).Components<ModelComponent>();
            if (ModelComponent* existing = models.Find(AliveId(L, entity)))
                existing->model = model ? model->handle : graphics::ModelHandler { };
            else if (model)
                models.Emplace(entity.id, ModelComponent { .model = model->handle });
        }

        String EntityToString(lua_State* L, const Entity& entity)
        {
            if (!IsValid(L, entity))
                return "Entity(destroyed)";
            return "Entity(" + NodeOf(L, entity).name + ")";
        }

        // ── Components ──────────────────────────────────────────────────────────
        // entity:add/remove/has and elm.scene.with take the component type by name.

        /// add("Script", init): init is a module name or { module = "npc.guard", props = { speed = 3 } }.
        ScriptComponent ReadScriptInit(lua_State* L, const std::optional<lua::Ref>& init)
        {
            if (!init)
                luaL_error(L, "add(\"Script\", init): init is a module name or { module = ..., props = ... }");
            init->Push(L);
            const int index = lua_gettop(L);
            ScriptComponent component;
            if (lua_type(L, index) == LUA_TSTRING) {
                component.module = *lua::StackTraits<String>::Get(L, index);
            } else if (lua_istable(L, index)) {
                // An unknown field is a typo, not something to skip
                lua_pushnil(L);
                while (lua_next(L, index)) {
                    const char* key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : nullptr;
                    if (!key || (std::strcmp(key, "module") != 0 && std::strcmp(key, "props") != 0))
                        luaL_error(L, "Script init has no field '%s' (module, props)", luaL_tolstring(L, -2, nullptr));
                    lua_pop(L, 1);
                }
                if (lua_getfield(L, index, "module") != LUA_TSTRING)
                    luaL_error(L, "Script init: module must be a string");
                component.module = *lua::StackTraits<String>::Get(L, -1);
                lua_pop(L, 1);
                if (lua_getfield(L, index, "props") != LUA_TNIL)
                    component.props = CheckScriptProps(L, -1);
                lua_pop(L, 1);
            } else {
                luaL_error(L, "add(\"Script\", init): init is a module name or a table, got %s", luaL_typename(L, index));
            }
            lua_pop(L, 1);
            if (component.module.empty())
                luaL_error(L, "Script init: the module name is empty");
            return component;
        }

        /// The Script's result: its instance table. The instance is made at once, so a broken module
        /// fails here; on_start runs on the next update.
        lua::Ref AddScript(lua_State* L, NodeId node, const std::optional<lua::Ref>& init)
        {
            ScriptComponent component = ReadScriptInit(L, init);
            SceneOf(L).Components<ScriptComponent>().Emplace(node, std::move(component));
            if (auto made = ScriptsOf(L).PushNew(node); !made) {
                SceneOf(L).Components<ScriptComponent>().Remove(node);
                luaL_error(L, "%s", made.error().message.c_str());
            }
            lua::Ref instance(L, -1);
            lua_pop(L, 1);
            return instance;
        }

        void EndScript(lua_State* L, NodeId node) { ScriptsOf(L).Destroy({ &node, 1 }); }

        constexpr ComponentKind ScriptKind { ComponentType::Script, &FindStore<ScriptComponent>, &StoreOf<ScriptComponent>, &AddScript, &EndScript };

        /// The component types entity:add/remove/has and elm.scene.with know. Model is not one of them:
        /// scripts reach it as entity.model, color and visible.
        std::array<const ComponentKind*, 3> LuaKinds() { return { &ScriptKind, &RigidBodyKind(), &ColliderKind() }; }

        /// "Script, RigidBody, Collider"
        String KnownComponentTypes()
        {
            String known;
            for (const ComponentKind* kind : LuaKinds()) {
                if (!known.empty())
                    known += ", ";
                known += ComponentTypeName(kind->type);
            }
            return known;
        }

        const ComponentKind& KindOf(lua_State* L, StringView name)
        {
            const auto type = ComponentTypeFromName(name);
            for (const ComponentKind* kind : LuaKinds()) {
                if (type == kind->type)
                    return *kind;
            }
            luaL_error(L, "unknown component type '%s' (known: %s)", String(name).c_str(), KnownComponentTypes().c_str());
            std::unreachable();
        }

        /// entity:add(type, init) -> for a Script its instance table, for RigidBody and Collider
        /// their handle (entity.rigidbody, entity.collider).
        lua::Ref AddComponent(lua_State* L, Entity& entity, StringView type, std::optional<lua::Ref> init)
        {
            const ComponentKind& kind = KindOf(L, type);
            const NodeId id = entity.id;
            IndexOf(L, entity);
            if (const auto* store = kind.find(SceneOf(L)); store && store->Contains(id))
                luaL_error(L, "%s already has a %s, remove it first", EntityToString(L, entity).c_str(), ComponentTypeName(kind.type));
            return kind.add(L, id, init);
        }

        /// entity:remove(type) -> whether there was one. A Script instance gets on_destroy first.
        bool RemoveComponent(lua_State* L, Entity& entity, StringView type)
        {
            const ComponentKind& kind = KindOf(L, type);
            const NodeId id = entity.id;
            IndexOf(L, entity);
            const auto* store = kind.find(SceneOf(L));
            if (!store || !store->Contains(id))
                return false;
            if (kind.beforeRemove)
                kind.beforeRemove(L, id);
            return kind.store(SceneOf(L)).Remove(id);
        }

        bool HasComponent(lua_State* L, const Entity& entity, StringView type)
        {
            const ComponentKind& kind = KindOf(L, type);
            IndexOf(L, entity);
            const auto* store = kind.find(SceneOf(L));
            return store && store->Contains(entity.id);
        }

        /// The instance table of the entity's script, nil without a Script.
        lua::Ref GetScript(lua_State* L, const Entity& entity)
        {
            IndexOf(L, entity);
            if (auto pushed = ScriptsOf(L).Push(entity.id); !pushed)
                luaL_error(L, "%s: %s", EntityToString(L, entity).c_str(), pushed.error().message.c_str());
            lua::Ref instance(L, -1);
            lua_pop(L, 1);
            return instance;
        }

        void RegisterEntity(lua::Module& module)
        {
            lua::Class<Entity>(module, "Entity")
                .Property<&IsValid>("valid")
                .Property<[](const Entity& entity) { return entity.id.Raw(); }>("id")
                .Property<&GetName, &SetName>("name")
                .Property<&GetVisible, &SetVisible>("visible")
                .Property<&GetColor, &SetColor>("color")
                .Property<&GetPosition, &SetPosition>("position")
                .Property<&GetRotation, &SetRotation>("rotation")
                .Property<&GetScale, &SetScale>("scale")
                .Property<&GetLocal, &SetLocal>("local_transform")
                .Property<&GetWorld, &SetWorld>("world_transform")
                .Property<&GetWorldPosition>("world_position")
                .Property<&GetParent, &SetParent>("parent")
                .Property<&GetModel, &SetModel>("model")
                .Property<&GetScript>("script")
                .Property<&GetRigidBody>("rigidbody")
                .Property<&GetCollider>("collider")
                .Method<&Translate>("translate", { "offset" })
                .Method<&Rotate>("rotate", { "axis", "radians" })
                .Method<&Children>("children")
                .Method<&AddComponent>("add", { "type", "init" })
                .Method<&RemoveComponent>("remove", { "type" })
                .Method<&HasComponent>("has", { "type" })
                .Method<&Destroy>("destroy")
                .Meta<&EntityToString>("__tostring");
        }

        // ── elm.scene ───────────────────────────────────────────────────────────

        Entity Spawn(lua_State* L, std::optional<String> name, std::optional<ModelRef> model, std::optional<Entity> parent)
        {
            auto& scene = SceneOf(L);
            if (model && !ModelsOf(L).Get(model->handle))
                luaL_error(L, "the model was removed");
            const NodeId parentNode = parent ? AliveId(L, *parent) : NodeId { };
            const NodeId node = scene.AddNode(name.value_or(String("Entity")), math::Matrix4x4::Identity(), parentNode);
            if (model)
                scene.Components<ModelComponent>().Emplace(node, ModelComponent { .model = model->handle });
            return { node };
        }

        std::optional<Entity> Find(lua_State* L, StringView name)
        {
            for (const auto& node : SceneOf(L).Nodes()) {
                if (node.name == name)
                    return Entity { node.id };
            }
            return std::nullopt;
        }

        Vector<Entity> FindAll(lua_State* L, StringView name)
        {
            Vector<Entity> found;
            for (const auto& node : SceneOf(L).Nodes()) {
                if (node.name == name)
                    found.push_back({ node.id });
            }
            return found;
        }

        Vector<Entity> All(lua_State* L)
        {
            const auto nodes = SceneOf(L).Nodes();
            Vector<Entity> all;
            all.reserve(nodes.size());
            for (const auto& node : nodes)
                all.push_back({ node.id });
            return all;
        }

        size_t Count(lua_State* L) { return SceneOf(L).NodeCount(); }

        /// Entities that have a component of the type, in scene order.
        Vector<Entity> With(lua_State* L, StringView type)
        {
            const ComponentKind& kind = KindOf(L, type);
            auto& scene = SceneOf(L);
            Vector<size_t> indices;
            if (const auto* store = kind.find(scene)) {
                for (const NodeId id : store->Ids()) {
                    if (const size_t index = scene.IndexOf(id); index != Scene::NoNode)
                        indices.push_back(index);
                }
            }
            std::sort(indices.begin(), indices.end());
            Vector<Entity> found;
            found.reserve(indices.size());
            for (const size_t index : indices)
                found.push_back(EntityAt(scene, index));
            return found;
        }

    } // namespace

    void RegisterScene(lua::State& state, Scene* scene, graphics::ModelManager* models, EntityScripts& entityScripts)
    {
        state.SetContext(SceneContext { scene, models, &entityScripts });
        lua::Module module(state, "elm.scene");
        module
            .Function<&Spawn>("spawn", { "name", "model", "parent" })
            .Function<&Find>("find", { "name" })
            .Function<&FindAll>("find_all", { "name" })
            .Function<&All>("all")
            .Function<&Count>("count")
            .Function<&With>("with", { "type" });
        RegisterEntity(module);
    }

} // namespace elm::script::bindings
