#include "script/bindings/Bindings.hpp"
#include "script/bindings/Components.hpp"
#include "script/bindings/Entity.hpp"
#include "script/bindings/ScriptValues.hpp"
#include "script/PlayModeController.hpp"
#include "script/ScriptSystem.hpp"

#include "Scene/ScriptComponent.hpp"
#include "core/AssetPaths.hpp"
#include "graphics/Camera.hpp"
#include "graphics/ModelManager.hpp"
#include "math/Rotation.hpp"
#include "resmgr/GltfExporter.hpp"
#include "resmgr/Serializer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace elm::script::bindings {

    namespace {

        using math::Matrix4x4;
        using math::Quaternion;
        using math::Vector3;
        using math::Vector4;

        Scene& SceneOf(lua_State* L) { return RequireSystem(L, Engine(L).scene, "elm.scene"); }
        graphics::ModelManager& ModelsOf(lua_State* L) { return RequireSystem(L, Engine(L).models, "elm.models"); }
        Camera& CameraOf(lua_State* L) { return RequireSystem(L, Engine(L).camera, "elm.camera"); }

        // ── Entity ──────────────────────────────────────────────────────────────

        size_t IndexOf(lua_State* L, const Entity& entity)
        {
            const size_t index = SceneOf(L).IndexOf(entity.id);
            if (index == Scene::NoNode)
                luaL_error(L, "the entity was destroyed");
            return index;
        }

        Scene::Node& NodeOf(lua_State* L, const Entity& entity) { return SceneOf(L).nodes[IndexOf(L, entity)]; }

        Entity EntityAt(Scene& scene, size_t index) { return { scene.IdOf(index) }; }

        bool IsValid(lua_State* L, const Entity& entity)
        {
            const Scene* scene = Engine(L).scene;
            return scene && scene->IndexOf(entity.id) != Scene::NoNode;
        }

        String GetName(lua_State* L, const Entity& entity) { return NodeOf(L, entity).name; }
        void SetName(lua_State* L, Entity& entity, String name) { NodeOf(L, entity).name = std::move(name); }
        bool GetVisible(lua_State* L, const Entity& entity) { return NodeOf(L, entity).visible; }
        void SetVisible(lua_State* L, Entity& entity, bool visible) { NodeOf(L, entity).visible = visible; }
        Vector4 GetColor(lua_State* L, const Entity& entity) { return NodeOf(L, entity).color; }
        void SetColor(lua_State* L, Entity& entity, const Vector4& color) { NodeOf(L, entity).color = color; }

        // Local transform, relative to the parent. Writes mark the scene dirty, world
        // transforms follow before culling (or on the next world_* read).

        Matrix4x4 GetLocal(lua_State* L, const Entity& entity) { return NodeOf(L, entity).localTransform; }
        void SetLocal(lua_State* L, Entity& entity, const Matrix4x4& transform)
        {
            SceneOf(L).SetLocalTransform(IndexOf(L, entity), transform);
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

        const Scene::Node& UpdatedNodeOf(lua_State* L, const Entity& entity)
        {
            SceneOf(L).UpdateWorldTransforms();
            return NodeOf(L, entity);
        }

        Matrix4x4 GetWorld(lua_State* L, const Entity& entity) { return UpdatedNodeOf(L, entity).worldTransform; }
        void SetWorld(lua_State* L, Entity& entity, const Matrix4x4& transform)
        {
            auto& scene = SceneOf(L);
            scene.UpdateWorldTransforms();
            if (!scene.SetWorldTransform(IndexOf(L, entity), transform))
                luaL_error(L, "cannot place the entity: its parent transform is singular");
        }
        Vector3 GetWorldPosition(lua_State* L, const Entity& entity)
        {
            const auto& m = UpdatedNodeOf(L, entity).worldTransform;
            return { m(0, 3), m(1, 3), m(2, 3) };
        }

        // Hierarchy

        std::optional<Entity> GetParent(lua_State* L, const Entity& entity)
        {
            auto& scene = SceneOf(L);
            const size_t parent = NodeOf(L, entity).parentIndex;
            if (parent >= scene.nodes.size())
                return std::nullopt;
            return EntityAt(scene, parent);
        }

        /// Reparents and keeps the world transform. nil detaches.
        void SetParent(lua_State* L, Entity& entity, std::optional<Entity> parent)
        {
            auto& scene = SceneOf(L);
            const size_t index = IndexOf(L, entity);
            scene.UpdateWorldTransforms();
            const Matrix4x4 world = scene.nodes[index].worldTransform;

            if (!parent) {
                scene.nodes[index].parentIndex = Scene::NoNode;
                scene.SetLocalTransform(index, world);
                return;
            }

            const size_t parentIndex = IndexOf(L, *parent);
            for (size_t current = parentIndex; current < scene.nodes.size(); current = scene.nodes[current].parentIndex) {
                if (current == index)
                    luaL_error(L, "an entity cannot become a child of itself or of its descendant");
            }
            const auto parentInverse = math::Inverse(scene.nodes[parentIndex].worldTransform);
            if (!parentInverse)
                luaL_error(L, "cannot attach to a parent with a singular transform");
            scene.nodes[index].parentIndex = parentIndex;
            scene.SetLocalTransform(index, *parentInverse * world);
        }

        Vector<Entity> Children(lua_State* L, const Entity& entity)
        {
            auto& scene = SceneOf(L);
            const size_t index = IndexOf(L, entity);
            Vector<Entity> children;
            for (size_t child = 0; child < scene.nodes.size(); ++child) {
                if (scene.nodes[child].parentIndex == index)
                    children.push_back(EntityAt(scene, child));
            }
            return children;
        }

        /// Removes the entity with all its descendants.
        void Destroy(lua_State* L, Entity& entity)
        {
            auto& scene = SceneOf(L);
            const size_t root = IndexOf(L, entity);
            Vector<bool> doomed(scene.nodes.size(), false);
            doomed[root] = true;
            // Parents may follow their children: repeat until no new descendant shows up
            for (bool grown = true; grown;) {
                grown = false;
                for (size_t index = 0; index < scene.nodes.size(); ++index) {
                    const size_t parent = scene.nodes[index].parentIndex;
                    if (!doomed[index] && parent < scene.nodes.size() && doomed[parent])
                        grown = doomed[index] = true;
                }
            }
            Vector<NodeId> doomedIds;
            for (size_t index = 0; index < scene.nodes.size(); ++index) {
                if (doomed[index])
                    doomedIds.push_back(scene.IdOf(index));
            }

            // Scripts get on_destroy while their entities still exist. It may change the scene,
            // so the nodes are found again by id afterwards.
            ScriptSystem::From(L).DestroyEntityScripts(doomedIds);
            Vector<size_t> indices;
            for (const NodeId id : doomedIds) {
                if (const size_t index = scene.IndexOf(id); index != Scene::NoNode)
                    indices.push_back(index);
            }
            // From the back: a removal shifts only the nodes after it
            std::sort(indices.begin(), indices.end(), std::greater<>());
            for (const size_t index : indices)
                scene.RemoveNode(index);
        }

        std::optional<ModelRef> GetModel(lua_State* L, const Entity& entity)
        {
            const auto handle = NodeOf(L, entity).model;
            if (!handle)
                return std::nullopt;
            return ModelRef { handle };
        }
        void SetModel(lua_State* L, Entity& entity, std::optional<ModelRef> model)
        {
            if (model && !ModelsOf(L).Get(model->handle))
                luaL_error(L, "the model was removed");
            NodeOf(L, entity).model = model ? model->handle : graphics::ModelHandler { };
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
            if (auto made = ScriptSystem::From(L).PushEntityScript(node, false); !made) {
                SceneOf(L).Components<ScriptComponent>().Remove(node);
                luaL_error(L, "%s", made.error().message.c_str());
            }
            lua::Ref instance(L, -1);
            lua_pop(L, 1);
            return instance;
        }

        void EndScript(lua_State* L, NodeId node) { ScriptSystem::From(L).DestroyEntityScripts({ &node, 1 }); }

        constexpr ComponentKind ScriptKind { "Script", &FindStore<ScriptComponent>, &StoreOf<ScriptComponent>, &AddScript, &EndScript };

        const ComponentKind& KindOf(lua_State* L, StringView type)
        {
            for (const ComponentKind* kind : { &ScriptKind, &RigidBodyKind(), &ColliderKind() }) {
                if (type == kind->name)
                    return *kind;
            }
            luaL_error(L, "unknown component type '%s' (known: Script, RigidBody, Collider)", String(type).c_str());
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
                luaL_error(L, "%s already has a %s, remove it first", EntityToString(L, entity).c_str(), kind.name);
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
            if (auto pushed = ScriptSystem::From(L).PushEntityScript(entity.id); !pushed)
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
            const size_t parentIndex = parent ? IndexOf(L, *parent) : Scene::NoNode;

            auto& node = scene.AddNode(model ? model->handle : graphics::ModelHandler { }, name.value_or(String("Entity")));
            node.parentIndex = parentIndex;
            const size_t index = scene.nodes.size() - 1;
            scene.SetLocalTransform(index, Matrix4x4::Identity());
            return EntityAt(scene, index);
        }

        std::optional<Entity> Find(lua_State* L, StringView name)
        {
            auto& scene = SceneOf(L);
            for (size_t index = 0; index < scene.nodes.size(); ++index) {
                if (scene.nodes[index].name == name)
                    return EntityAt(scene, index);
            }
            return std::nullopt;
        }

        Vector<Entity> FindAll(lua_State* L, StringView name)
        {
            auto& scene = SceneOf(L);
            Vector<Entity> found;
            for (size_t index = 0; index < scene.nodes.size(); ++index) {
                if (scene.nodes[index].name == name)
                    found.push_back(EntityAt(scene, index));
            }
            return found;
        }

        Vector<Entity> All(lua_State* L)
        {
            auto& scene = SceneOf(L);
            Vector<Entity> all;
            all.reserve(scene.nodes.size());
            for (size_t index = 0; index < scene.nodes.size(); ++index)
                all.push_back(EntityAt(scene, index));
            return all;
        }

        size_t Count(lua_State* L) { return SceneOf(L).nodes.size(); }

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

        // ── Model, elm.models ───────────────────────────────────────────────────

        const graphics::Model& ModelOf(lua_State* L, const ModelRef& model)
        {
            const auto* found = ModelsOf(L).Get(model.handle);
            if (!found)
                luaL_error(L, "the model was removed");
            return *found;
        }

        bool IsModelValid(lua_State* L, const ModelRef& model)
        {
            const auto* models = Engine(L).models;
            return models && models->Get(model.handle);
        }

        ModelRef Insert(lua_State* L, graphics::Model model) { return { ModelsOf(L).Insert(std::move(model)) }; }

        ModelRef MakePrimitive(lua_State* L, const char* name, MeshData mesh)
        {
            graphics::Model model { String(name) };
            model.AddSubmesh(std::move(mesh));
            return Insert(L, std::move(model));
        }

        ModelRef Cube(lua_State* L, std::optional<float> size)
        {
            return MakePrimitive(L, "Cube", GeometryPrimitives::CreateCube(size.value_or(1.0f)));
        }
        ModelRef Box(lua_State* L, const Vector3& halfExtents) { return MakePrimitive(L, "Box", GeometryPrimitives::CreateBox(halfExtents)); }
        ModelRef Plane(lua_State* L, float sizeX, float sizeZ)
        {
            return MakePrimitive(L, "Plane", GeometryPrimitives::CreateGroundPlane(sizeX, sizeZ));
        }

        /// A .model or .gltf file; a relative path starts at the assets folder.
        EngineResult<ModelRef> Load(lua_State* L, StringView file)
        {
            ModelsOf(L);
            const Path relative(file);
            const Path path = relative.IsAbsolute() ? relative : AssetPaths::Root() / relative;
            const bool gltf = path.Extension().string() == ".gltf";
            auto model = gltf ? resmgr::GltfExporter::LoadModel(path) : resmgr::Serializer::LoadModel(path);
            if (!model)
                return MakeUnexpected(model.error());
            return Insert(L, std::move(*model));
        }

        std::optional<ModelRef> FindModel(lua_State* L, StringView name)
        {
            std::optional<ModelRef> found;
            ModelsOf(L).ForEach([&](graphics::ModelHandler handle, const graphics::Model& model) {
                if (!found && model.name == name)
                    found = ModelRef { handle };
            });
            return found;
        }

        void RegisterModels(lua::State& state)
        {
            lua::Module module(state, "elm.models");
            module
                .Function<&Cube>("cube", { "size" })
                .Function<&Box>("box", { "half_extents" })
                .Function<&Plane>("plane", { "size_x", "size_z" })
                .Function<&Load>("load", { "path" })
                .Function<&FindModel>("find", { "name" });

            lua::Class<ModelRef>(module, "Model")
                .Property<&IsModelValid>("valid")
                .Property<[](lua_State* L, const ModelRef& model) { return ModelOf(L, model).name; }>("name")
                .Property<[](lua_State* L, const ModelRef& model) { return ModelOf(L, model).submeshes.size(); }>("submesh_count")
                .Meta<[](lua_State* L, const ModelRef& model) {
                    return IsModelValid(L, model) ? "Model(" + ModelOf(L, model).name + ")" : String("Model(removed)");
                }>("__tostring");
        }

        // ── elm.engine: scene commands, the same as the Scene Hierarchy window ──

        /// Replaces the scene and all models with a built-in test scene (0 Box, 1 WallAndGrid,
        /// 2 RoomsAndCorridors, 3 PhysicsSandbox).
        void LoadTestScene(lua_State* L, uint32_t preset, std::optional<uint32_t> instances)
        {
            if (preset > static_cast<uint32_t>(ScenePreset::PhysicsSandbox))
                luaL_argerror(L, 1, "preset must be 0..3");
            ScriptSystem::From(L).DestroyAllEntityScripts();
            TestScenes::BuildScene(static_cast<ScenePreset>(preset), instances.value_or(1500), SceneOf(L), ModelsOf(L));
        }

        /// Removes every node and model; entity scripts get on_destroy first.
        void ClearScene(lua_State* L)
        {
            SceneOf(L);
            ScriptSystem::From(L).DestroyAllEntityScripts();
            SceneOf(L).Clear();
            if (auto* models = Engine(L).models)
                models->Clear();
        }

        /// A relative scene file is taken from assets/scenes.
        Path ScenePath(StringView file)
        {
            const Path path(file);
            return path.IsAbsolute() ? path : AssetPaths::Scenes() / path;
        }

        EngineResult<void> SaveScene(lua_State* L, StringView file)
        {
            return resmgr::Serializer::SaveScene(SceneOf(L), ModelsOf(L), ScenePath(file), AssetPaths::Models());
        }

        /// Replaces the scene with a scene file; returns how many nodes it has.
        EngineResult<size_t> LoadScene(lua_State* L, StringView file)
        {
            auto& scene = SceneOf(L);
            auto& models = ModelsOf(L);
            auto loaded = resmgr::Serializer::LoadScene(ScenePath(file));
            if (!loaded)
                return MakeUnexpected(loaded.error());
            ScriptSystem::From(L).DestroyAllEntityScripts();
            scene.Clear();
            models.Clear();
            scene.Append(std::move(*loaded), models);
            return scene.nodes.size();
        }

        // Play mode, applied at the start of the next update
        void PlayScene(lua_State* L)
        {
            if (auto* playMode = Engine(L).playMode)
                playMode->RequestPlay();
        }
        void StopScene(lua_State* L)
        {
            if (auto* playMode = Engine(L).playMode)
                playMode->RequestStop();
        }
        bool IsPlaying(lua_State* L)
        {
            const auto* playMode = Engine(L).playMode;
            return playMode && playMode->IsPlaying();
        }

        void RegisterEngine(lua::State& state)
        {
            lua::Module(state, "elm.engine")
                .Function<&LoadTestScene>("load_test_scene", { "preset", "instances" })
                .Function<&ClearScene>("clear_scene")
                .Function<&SaveScene>("save_scene", { "file" })
                .Function<&LoadScene>("load_scene", { "file" })
                .Function<&PlayScene>("play")
                .Function<&StopScene>("stop")
                .Function<&IsPlaying>("is_playing");
        }

        // ── elm.camera ──────────────────────────────────────────────────────────

        /// yaw and pitch that look along a direction (Camera: forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)).
        void LookAt(lua_State* L, const Vector3& target)
        {
            auto& camera = CameraOf(L);
            const Vector3 direction = (target - camera.GetPosition()).Normalized();
            if (direction.Length() == 0.0f)
                return;
            camera.SetOrientation(std::atan2(direction.x, direction.z), std::asin(direction.y));
        }

        void RegisterCamera(lua::State& state)
        {
            lua::Module(state, "elm.camera")
                .Function<[](lua_State* L) { return CameraOf(L).GetPosition(); }>("position")
                .Function<[](lua_State* L, const Vector3& position) { CameraOf(L).SetPosition(position); }>("set_position", { "position" })
                .Function<[](lua_State* L, const Vector3& offset) { CameraOf(L).Translate(offset); }>("translate", { "offset" })
                .Function<[](lua_State* L) { return CameraOf(L).GetForwardDirection(); }>("forward")
                .Function<[](lua_State* L) { return CameraOf(L).GetRightDirection(); }>("right")
                .Function<[](lua_State* L) { return CameraOf(L).GetYaw(); }>("yaw")
                .Function<[](lua_State* L) { return CameraOf(L).GetPitch(); }>("pitch")
                .Function<[](lua_State* L, float yaw, float pitch) { CameraOf(L).SetOrientation(yaw, pitch); }>("set_orientation", { "yaw", "pitch" })
                .Function<[](lua_State* L, float yaw, float pitch) { CameraOf(L).Rotate(yaw, pitch); }>("rotate", { "yaw", "pitch" })
                .Function<&LookAt>("look_at", { "target" })
                .Function<[](lua_State* L) { return CameraOf(L).fovRadians; }>("fov")
                .Function<[](lua_State* L, float radians) { CameraOf(L).fovRadians = radians; }>("set_fov", { "radians" });
        }

    } // namespace

    void RegisterScene(lua::State& state)
    {
        {
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
        RegisterModels(state);
        RegisterCamera(state);
        RegisterEngine(state);
    }

} // namespace elm::script::bindings
