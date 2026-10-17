#include "script/SelfTest.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"
#include "script/console/ScriptConsole.hpp"
#include "script/entity/EntityScripts.hpp"
#include "script/entity/ScriptDefaults.hpp"
#include "script/lua/Bind.hpp"
#include "script/module/MainModule.hpp"
#include "script/module/ModuleLoader.hpp"
#include "script/module/ModulePaths.hpp"
#include "script/reload/HotReload.hpp"
#include "script/ui/LuaHighlighter.hpp"
#include "script/ui/ScriptEditor.hpp"

#include "core/Memory.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/NodeIdTable.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneAsset.hpp"
#include "Scene/ScriptComponent.hpp"
#include "Scene/TestScenes.hpp"
#include "graphics/Camera.hpp"
#include "input/Input.hpp"
#include "physics/PhysicsSystem.hpp"
#include "resmgr/Serializer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace elm::script {

    /// The self-tests check the parts of a ScriptSystem directly: they are its friend.
    struct SelfTestAccess {
        static lua::State& State(ScriptSystem& system) { return system.m_state; }
        static const ModulePaths& Paths(ScriptSystem& system) { return *system.m_modulePaths; }
        static ModuleLoader& Loader(ScriptSystem& system) { return *system.m_moduleLoader; }
        static MainModule& Main(ScriptSystem& system) { return *system.m_mainModule; }
        static HotReload& Reload(ScriptSystem& system) { return *system.m_hotReload; }
        static ScriptDefaults& Defaults(ScriptSystem& system) { return *system.m_scriptDefaults; }
        static EntityScripts& Entities(ScriptSystem& system) { return *system.m_entityScripts; }
        static ScriptConsole& Console(ScriptSystem& system) { return *system.m_console; }
        static void RequestRestart(ScriptSystem& system) { system.m_restartRequested = true; }
    };

    namespace {

        // ── Test fixtures ───────────────────────────────────────────────────────

        struct TestVector {
            float x { 0.0f };
            float y { 0.0f };
            float z { 0.0f };

            TestVector() = default;
            TestVector(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) { }

            [[nodiscard]] float Dot(const TestVector& other) const noexcept { return x * other.x + y * other.y + z * other.z; }
            [[nodiscard]] float Length() const noexcept { return std::sqrt(Dot(*this)); }
            void Scale(float factor) noexcept
            {
                x *= factor;
                y *= factor;
                z *= factor;
            }
            bool operator==(const TestVector&) const = default;
        };

        TestVector Add(const TestVector& left, const TestVector& right) { return { left.x + right.x, left.y + right.y, left.z + right.z }; }
        String VectorToString(const TestVector& value)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "TestVector(%g, %g, %g)", value.x, value.y, value.z);
            return buffer;
        }
        void Normalize(TestVector& value)
        {
            if (const float length = value.Length(); length > 0.0f)
                value.Scale(1.0f / length);
        }

        /// Counts live instances, to check that __gc destroys every copy Lua holds.
        struct Tracked {
            static inline int alive = 0;
            int value { 0 };
            const int id { 7 };

            explicit Tracked(int inValue) : value(inValue) { ++alive; }
            Tracked(const Tracked& other) : value(other.value), id(other.id) { ++alive; }
            Tracked(Tracked&& other) noexcept : value(other.value), id(other.id) { ++alive; }
            Tracked& operator=(const Tracked&) = delete;
            ~Tracked() { --alive; }

            [[nodiscard]] int Doubled() const noexcept { return value * 2; }
        };

        int GetTrackedValue(const Tracked& tracked) { return tracked.value; }
        void SetTrackedValue(Tracked& tracked, int value)
        {
            if (value < 0)
                throw std::invalid_argument("value must not be negative");
            tracked.value = value;
        }

        enum class Color : uint8_t { Red = 1, Green = 2, Blue = 3 };

        Color NextColor(Color color) { return color == Color::Blue ? Color::Red : static_cast<Color>(static_cast<uint8_t>(color) + 1); }
        int Twice(int value) { return value * 2; }
        uint8_t Byte(uint8_t value) { return value; }
        StringView Echo(StringView text) { return text; }
        String Concat(const String& left, const char* right) { return left + right; }
        std::tuple<int, String, bool> Triple(int value) { return { value, String("two"), true }; }
        std::optional<int> Maybe(bool present) { return present ? std::optional<int>(5) : std::nullopt; }
        int OrDefault(std::optional<int> value) { return value.value_or(-1); }
        void Throw() { throw std::runtime_error("thrown from C++"); }
        EngineResult<int> Checked(bool fail)
        {
            if (fail)
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "checked failure"));
            return 10;
        }
        EngineResult<void> CheckedVoid(bool fail)
        {
            if (fail)
                return MakeUnexpected(EngineError(ErrorCode::ScriptError, "void failure"));
            return { };
        }
        /// Calls back into Lua from inside a Lua call (a nested protected call).
        int CallBack(const lua::FunctionRef& function, int value)
        {
            auto result = function.Call<int>(value);
            return result ? *result : -1;
        }
        /// The calling state is injected and takes no argument slot.
        int StackSize(lua_State* L, int ignored)
        {
            (void)ignored;
            return lua_gettop(L);
        }
        /// Raw Lua C function: any number of arguments.
        int Sum(lua_State* L)
        {
            lua_Number total = 0;
            for (int index = 1; index <= lua_gettop(L); ++index)
                total += luaL_checknumber(L, index);
            lua_pushnumber(L, total);
            return 1;
        }

        /// A Lua error raised while a C++ object is alive: the object must be destroyed on the way out,
        /// which holds only when Lua is compiled as C++ (exceptions instead of longjmp).
        void RaiseWithLiveObject(lua_State* L)
        {
            const Tracked guard(1);
            luaL_error(L, "raised over a live C++ object");
        }

        lua::Ref g_storedValue;
        void Store(lua::Ref value) { g_storedValue = std::move(value); }
        int RestoreRaw(lua_State* L)
        {
            g_storedValue.Push(L);
            return 1;
        }

        void RegisterFixtures(lua::State& state)
        {
            lua::Module module(state, "elm_test");
            module
                .Function<&Twice>("twice", { "value" })
                .Function<&Byte>("byte", { "value" })
                .Function<&NextColor>("next_color", { "color" })
                .Function<&Echo>("echo", { "text" })
                .Function<&Concat>("concat", { "left", "right" })
                .Function<&Triple>("triple", { "value" })
                .Function<&Maybe>("maybe", { "present" })
                .Function<&OrDefault>("or_default", { "value" })
                .Function<&Throw>("throw")
                .Function<&RaiseWithLiveObject>("raise_with_live_object")
                .Function<&Checked>("checked", { "fail" })
                .Function<&CheckedVoid>("checked_void", { "fail" })
                .Function<&CallBack>("call_back", { "fn", "value" })
                .Function<&StackSize>("stack_size", { "ignored" })
                .Function<&Sum>("sum")
                .Function<&Store>("store", { "value" })
                .Function<&RestoreRaw>("restore")
                .Function<[](int left, int right) { return left - right; }>("subtract", { "left", "right" })
                .Function<[] { return Tracked::alive; }>("tracked_alive")
                .Value("answer", 42)
                .Value("name", "elm");

            lua::Class<TestVector>(module, "TestVector")
                .Constructor<float, float, float>({ "x", "y", "z" })
                .Field<&TestVector::x>("x")
                .Field<&TestVector::y>("y")
                .Field<&TestVector::z>("z")
                .Property<&TestVector::Length>("length")
                .Method<&TestVector::Dot>("dot", { "other" })
                .Method<&TestVector::Scale>("scale", { "factor" })
                .Method<&Normalize>("normalize")
                .StaticFunction<[] { return TestVector(0.0f, 1.0f, 0.0f); }>("up")
                .Meta<&Add>("__add")
                .Meta<&VectorToString>("__tostring");

            lua::Class<Tracked>(module, "Tracked")
                .Constructor<int>({ "value" })
                .Field<&Tracked::id>("id")
                .Property<&GetTrackedValue, &SetTrackedValue>("value")
                .Method<&Tracked::Doubled>("doubled");
        }

        // ── Reporting ───────────────────────────────────────────────────────────

        struct Report {
            int passed { 0 };
            int failed { 0 };

            void Check(bool condition, StringView name, StringView detail = { })
            {
                if (condition) {
                    ++passed;
                    std::cout << "[PASS] " << name << "\n";
                } else {
                    ++failed;
                    std::cout << "[FAIL] " << name << (detail.empty() ? "" : ": ") << detail << "\n";
                }
            }
        };

        // ── C++ side ────────────────────────────────────────────────────────────

        void RunNativeTests(ScriptSystem& system, Report& report)
        {
            auto& state = SelfTestAccess::State(system);
            lua_State* L = state;
            const int top = lua_gettop(L);

            auto loadFunction = [&](const char* code) -> lua::FunctionRef {
                if (!state.Load(code, "=native") || !state.ProtectedCall(0, 1))
                    return { };
                lua::FunctionRef function(L, -1);
                lua_pop(L, 1);
                return function;
            };

            {
                const auto add = loadFunction("return function(a, b) return a + b end");
                const auto sum = add.Call<int>(2, 3);
                report.Check(sum && *sum == 5, "native::call_returns_value");
                const auto text = add.Call<String>(2, 3);
                report.Check(!text, "native::call_rejects_wrong_result_type");
            }
            {
                const auto fail = loadFunction("return function() error('script failure') end");
                const auto result = fail.Call();
                report.Check(!result && result.error().code == ErrorCode::ScriptError
                        && result.error().message.find("script failure") != String::npos
                        && result.error().message.find("stack traceback") != String::npos,
                    "native::call_error_has_traceback", result ? "" : result.error().message);
            }
            {
                const auto vector = loadFunction("return function() return elm_test.TestVector(1, 2, 3) end");
                const auto value = vector.Call<TestVector>();
                report.Check(value && *value == TestVector(1.0f, 2.0f, 3.0f), "native::call_returns_userdata");
                const auto take = loadFunction("return function(v) return v.x + v.y + v.z end");
                const auto total = take.Call<float>(TestVector(1.0f, 2.0f, 3.0f));
                report.Check(total && *total == 6.0f, "native::call_passes_userdata");
            }
            {
                lua::FunctionRef empty;
                report.Check(!empty.Call(), "native::empty_reference_fails");
                auto add = loadFunction("return function(a, b) return a + b end");
                lua::FunctionRef moved = std::move(add);
                report.Check(!add.IsValid() && moved.IsValid(), "native::reference_moves");
            }
            {
                const auto result = state.Run("local x = ", "=syntax");
                report.Check(!result && result.error().message.find("syntax") != String::npos, "native::syntax_error_reported");
            }
            {
                // The console's print outside a console command only logs
                const auto result = state.Run("local keep = print keep('print outside a command')", "=native");
                report.Check(result.has_value(), "native::print_outside_a_command");
            }

            const String meta = state.Api().GenerateLuaMeta();
            report.Check(meta.find("---@class TestVector") != String::npos
                    && meta.find("function TestVector:dot(other) end") != String::npos
                    && meta.find("---@field length number (read only)") != String::npos
                    && meta.find("function elm_test.twice(value) end") != String::npos
                    && meta.find("---@overload fun(x: number, y: number, z: number): TestVector") != String::npos,
                "native::api_meta_generated");

            report.Check(state.MemoryUsage() > 0, "native::memory_is_tracked");
            report.Check(lua_gettop(L) == top, "native::stack_balanced");
        }

        // ── Lua state ───────────────────────────────────────────────────────────

        void RunStateTests(Report& report)
        {
            struct First {
                int value { 0 };
            };
            struct Second {
                int value { 0 };
            };
            lua::State state;
            state.SetContext(First { 1 });
            state.SetContext(Second { 2 });
            state.SetContext(First { 3 });
            report.Check(lua::State::Context<First>(state)->value == 3 && lua::State::Context<Second>(state)->value == 2,
                "state::contexts_by_type");

            lua::Module(state, "elm_reset").Value("answer", 42);
            state.Reset();
            const bool empty = state.IsOpen() && !lua::State::Context<First>(state) && state.Api().Modules().empty()
                && lua_getglobal(state, "elm_reset") == LUA_TNIL;
            lua_settop(state, 0);
            report.Check(empty, "state::reset_opens_an_empty_state");
            state.Close();
            report.Check(!state.IsOpen(), "state::close");
        }

        // ── Scene node ids ──────────────────────────────────────────────────────

        void RunNodeIdTableTests(Report& report)
        {
            NodeIdTable ids;
            const NodeId a = ids.Allocate(0);
            const NodeId b = ids.Allocate(1);
            report.Check(a && b && a != b && ids.IndexOf(a) == 0 && ids.IndexOf(b) == 1 && ids.IndexOf({ }) == NodeIdTable::NoIndex,
                "node_ids::allocate");
            ids.Move(b, 7);
            report.Check(ids.IndexOf(b) == 7, "node_ids::move");
            report.Check(ids.Free(a) && !ids.Free(a) && ids.IndexOf(a) == NodeIdTable::NoIndex, "node_ids::free");
            const NodeId reused = ids.Allocate(0);
            report.Check(reused.Index() == a.Index() && reused.Generation() == a.Generation() + 1 && ids.IndexOf(a) == NodeIdTable::NoIndex,
                "node_ids::reused_slot_new_generation");

            // A slot that gave out every generation is retired
            NodeIdTable worn;
            NodeId last = worn.Allocate(0);
            for (int round = 0; round < 255; ++round) {
                worn.Free(last);
                last = worn.Allocate(0);
            }
            worn.Free(last);
            const NodeId next = worn.Allocate(0);
            report.Check(last.Generation() == 255 && next.Index() != last.Index() && worn.IndexOf(last) == NodeIdTable::NoIndex,
                "node_ids::exhausted_slot_retires");

            // An id given out during Play stays stale after Stop, also once the restored node frees the slot
            NodeIdTable live;
            const NodeId kept = live.Allocate(0);
            NodeIdTable snapshot = live;
            live.Free(kept);
            const NodeId playTime = live.Allocate(0);
            live.RestoreFrom(std::move(snapshot));
            report.Check(live.IndexOf(kept) == 0 && live.IndexOf(playTime) == NodeIdTable::NoIndex, "node_ids::restore_brings_back_ids");
            live.Free(kept);
            const NodeId afterStop = live.Allocate(0);
            report.Check(afterStop.Index() == playTime.Index() && afterStop != playTime && live.IndexOf(playTime) == NodeIdTable::NoIndex,
                "node_ids::play_id_stays_stale_after_stop");
        }

        /// World transform of a live node, pending changes included.
        Matrix4x4 WorldOf(Scene& scene, NodeId node)
        {
            scene.UpdateWorldTransforms();
            return scene.WorldTransforms()[scene.IndexOf(node)];
        }

        /// Every node comes after its parent.
        bool ParentsComeFirst(const Scene& scene)
        {
            const auto nodes = scene.Nodes();
            for (size_t index = 0; index < nodes.size(); ++index) {
                const size_t parent = scene.IndexOf(nodes[index].parent);
                if (nodes[index].parent && (parent == Scene::NoNode || parent >= index))
                    return false;
            }
            return scene.WorldTransforms().size() == nodes.size();
        }

        void RunSceneTests(Report& report)
        {
            Scene scene;
            const NodeId a = scene.AddNode({ .name = "a" });
            const NodeId b = scene.AddNode({ .name = "b" });
            const NodeId c = scene.AddNode({ .name = "c" });
            report.Check(a && b && c && a != b && b != c, "scene::ids_assigned");
            report.Check(scene.IndexOf(c) == 2 && scene.Find(b)->name == "b" && scene.IdOf(2) == c && !scene.IdOf(3),
                "scene::ids_resolve");

            // b becomes the parent of c, c sits 1 unit above b
            report.Check(scene.SetParent(c, b) == Scene::SetParentResult::Done, "scene::set_parent");
            scene.SetLocalTransform(b, Matrix4x4::Translation(Vector3 { 5.0f, 0.0f, 0.0f }));
            scene.SetLocalTransform(c, Matrix4x4::Translation(Vector3 { 0.0f, 1.0f, 0.0f }));
            const Matrix4x4 world = WorldOf(scene, c);
            report.Check(world(0, 3) == 5.0f && world(1, 3) == 1.0f, "scene::world_follows_parent");

            report.Check(scene.RemoveNode(a), "scene::remove");
            report.Check(scene.IndexOf(a) == Scene::NoNode && !scene.Find(a), "scene::removed_id_is_stale");
            report.Check(scene.IndexOf(b) == 0 && scene.IndexOf(c) == 1 && scene.Find(c)->parent == b,
                "scene::ids_follow_moved_nodes");

            // Removing a parent keeps its child where it was in the world
            report.Check(scene.RemoveNode(b), "scene::remove_parent");
            const Matrix4x4 childWorld = WorldOf(scene, c);
            report.Check(scene.Find(c) && !scene.Find(c)->parent && childWorld(0, 3) == 5.0f && childWorld(1, 3) == 1.0f,
                "scene::child_keeps_world_transform");

            const NodeId d = scene.AddNode({ .name = "d" });
            report.Check(d != a && d != b && scene.IndexOf(a) == Scene::NoNode, "scene::reused_slot_new_generation");

            // Every write goes through the scene; a stale id changes nothing
            report.Check(!scene.SetName(a, "x") && !scene.SetWorldTransform(b, Matrix4x4::Identity())
                    && !scene.SetLocalTransform(a, Matrix4x4::Identity()) && !scene.RemoveNode(a)
                    && scene.SetParent(d, a) == Scene::SetParentResult::StaleNode,
                "scene::setters_reject_stale_id");
            report.Check(scene.SetName(d, "renamed") && scene.Find(d)->name == "renamed", "scene::setters_change_the_node");

            scene.Clear();
            report.Check(scene.IndexOf(c) == Scene::NoNode && scene.IndexOf(d) == Scene::NoNode, "scene::clear_releases_ids");
        }

        void RunSceneHierarchyTests(Report& report)
        {
            Scene scene;
            const NodeId parent = scene.AddNode({ .name = "parent", .localTransform = Matrix4x4::Translation(Vector3 { 2.0f, 0.0f, 0.0f }) });
            const NodeId child = scene.AddNode({
                .name = "child",
                .parent = parent,
                .localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 3.0f, 0.0f }),
            });
            const Matrix4x4 childWorld = WorldOf(scene, child);
            report.Check(childWorld(0, 3) == 2.0f && childWorld(1, 3) == 3.0f && scene.Find(child)->parent == parent,
                "scene::add_node_follows_parent");

            const NodeId gone = scene.AddNode({ .name = "gone" });
            scene.RemoveNode(gone);
            report.Check(!scene.AddNode({ .name = "orphan", .parent = gone }) && scene.NodeCount() == 2, "scene::add_node_with_stale_parent");

            // Reparenting keeps the world transform; a parent that comes later takes the child after it
            const NodeId other = scene.AddNode({ .name = "other", .localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.0f, 4.0f }) });
            report.Check(scene.SetParent(child, other) == Scene::SetParentResult::Done, "scene::set_parent_done");
            report.Check(scene.IndexOf(other) == 1 && scene.IndexOf(child) == 2 && ParentsComeFirst(scene),
                "scene::set_parent_moves_the_child_after_its_parent");
            const Matrix4x4 movedWorld = WorldOf(scene, child);
            report.Check(movedWorld(0, 3) == 2.0f && movedWorld(1, 3) == 3.0f && scene.Find(child)->localTransform(2, 3) == -4.0f,
                "scene::set_parent_keeps_world");
            report.Check(scene.SetParent(other, child) == Scene::SetParentResult::Cycle
                    && scene.SetParent(child, child) == Scene::SetParentResult::Cycle,
                "scene::set_parent_rejects_cycle");
            report.Check(scene.SetParent(child, { }) == Scene::SetParentResult::Done && !scene.Find(child)->parent
                    && scene.Find(child)->localTransform(0, 3) == 2.0f,
                "scene::set_parent_detaches");
            const NodeId flat = scene.AddNode({ .name = "flat", .localTransform = Matrix4x4::Scaling(Vector3 { 1.0f, 0.0f, 1.0f }) });
            report.Check(scene.SetParent(child, flat) == Scene::SetParentResult::SingularParent, "scene::set_parent_singular");
        }

        void RunSceneOrderTests(Report& report)
        {
            // x has y, y has w; q stands between x and the new parent z
            Scene scene;
            const NodeId x = scene.AddNode({ .name = "x", .localTransform = Matrix4x4::Translation(Vector3 { 1.0f, 0.0f, 0.0f }) });
            const NodeId y = scene.AddNode({ .name = "y", .parent = x });
            const NodeId q = scene.AddNode({ .name = "q" });
            const NodeId z = scene.AddNode({ .name = "z", .localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 0.0f, 7.0f }) });
            const NodeId w = scene.AddNode({ .name = "w", .parent = y, .localTransform = Matrix4x4::Translation(Vector3 { 0.0f, 2.0f, 0.0f }) });
            report.Check(scene.SetParent(x, z) == Scene::SetParentResult::Done && scene.IndexOf(q) == 0 && scene.IndexOf(z) == 1
                    && scene.IndexOf(x) == 2 && scene.IndexOf(y) == 3 && scene.IndexOf(w) == 4 && ParentsComeFirst(scene),
                "scene::subtree_moves_after_a_later_parent");
            const Matrix4x4 world = WorldOf(scene, w);
            report.Check(std::abs(world(0, 3) - 1.0f) < 1e-5f && std::abs(world(1, 3) - 2.0f) < 1e-5f && std::abs(world(2, 3)) < 1e-5f,
                "scene::moved_subtree_keeps_world");
            report.Check(scene.RemoveNode(z) && scene.Find(x) && !scene.Find(x)->parent && ParentsComeFirst(scene),
                "scene::removed_parent_keeps_the_order");

            // A scene file may list a parent after its child: the parent goes in first
            SceneAsset asset;
            asset.nodes.resize(2);
            asset.nodes[0].name = "child";
            asset.nodes[0].parentIndex = 1;
            asset.nodes[1].name = "parent";
            asset.components.Components<ModelComponent>().Emplace(SceneAsset::NodeRef(0),
                ModelComponent { .model = SceneAsset::ModelRef(0), .color = { 1.0f, 0.0f, 0.0f, 1.0f } });
            Scene target;
            (void)target.AddNode({ .name = "existing" });
            const graphics::ModelHandler model(3, 1);
            const Vector<NodeId> ids = target.Append(std::move(asset), std::span(&model, 1));
            report.Check(ids.size() == 2 && target.IndexOf(ids[1]) == 1 && target.IndexOf(ids[0]) == 2 && target.Find(ids[0])->name == "child"
                    && target.Find(ids[0])->parent == ids[1] && ParentsComeFirst(target),
                "scene::append_parent_after_child");
            const auto* models = target.FindComponents<ModelComponent>();
            const ModelComponent* childModel = models ? models->Find(ids[0]) : nullptr;
            report.Check(childModel && childModel->model == model && childModel->color.x == 1.0f && !models->Contains(ids[1]),
                "scene::append_gives_models_their_handles");
        }

        // ── Scene snapshots ─────────────────────────────────────────────────────

        void RunSnapshotTests(Report& report)
        {
            Scene scene;
            const NodeId a = scene.AddNode({ .name = "a" });
            const NodeId b = scene.AddNode({ .name = "b" });
            scene.Components<RigidBody>().Emplace(a, RigidBody { .mass = 7.0f });
            Scene copy = scene.Clone();
            report.Check(copy.IndexOf(a) == 0 && copy.IndexOf(b) == 1 && copy.FindComponents<RigidBody>()
                    && copy.FindComponents<RigidBody>()->Find(a)->mass == 7.0f,
                "snapshot::clone_keeps_ids_and_components");

            // Meanwhile: c comes in a new slot, b goes, a changes
            const NodeId c = scene.AddNode({ .name = "c" });
            report.Check(scene.RemoveNode(b), "snapshot::play_removes");
            scene.Components<RigidBody>().Remove(a);
            scene.SetName(a, "changed");
            scene.RestoreFrom(std::move(copy));
            report.Check(scene.NodeCount() == 2 && scene.IndexOf(a) == 0 && scene.IndexOf(b) == 1 && scene.Find(a)->name == "a"
                    && scene.Components<RigidBody>().Find(a) && scene.IndexOf(c) == Scene::NoNode,
                "snapshot::restore_brings_back_nodes_and_ids");
            // c's slot is free again, at a newer generation: c's id stays stale
            const NodeId d = scene.AddNode({ .name = "d" });
            report.Check(d.Index() == c.Index() && d != c && scene.IndexOf(c) == Scene::NoNode && scene.IndexOf(d) == 2,
                "snapshot::ids_given_out_meanwhile_stay_stale");

            const NodeId e = scene.AddNode({ .name = "e" });
            scene.CopyComponents(a, e);
            report.Check(scene.Components<RigidBody>().Find(e) && scene.Components<RigidBody>().Find(e)->mass == 7.0f,
                "snapshot::copy_components");
        }

        // ── Component stores ────────────────────────────────────────────────────

        void RunComponentStoreTests(Report& report)
        {
            struct Health {
                int value { 0 };
            };

            Scene scene;
            const NodeId a = scene.AddNode({ .name = "a" });
            const NodeId b = scene.AddNode({ .name = "b" });
            const NodeId c = scene.AddNode({ .name = "c" });
            report.Check(!scene.FindComponents<RigidBody>(), "components::no_store_before_use");
            auto& bodies = scene.Components<RigidBody>();
            bodies.Emplace(a, RigidBody { .mass = 10.0f });
            bodies.Emplace(b, RigidBody { .mass = 20.0f });
            bodies.Emplace(c, RigidBody { .mass = 30.0f });
            report.Check(bodies.Size() == 3 && bodies.Find(b)->mass == 20.0f && scene.FindComponents<RigidBody>() == &bodies,
                "components::emplace_and_find");
            bodies.Emplace(b, RigidBody { .mass = 25.0f });
            report.Check(bodies.Size() == 3 && bodies.Find(b)->mass == 25.0f && bodies.Ids()[1] == b, "components::emplace_replaces_in_place");

            report.Check(bodies.Remove(a) && !bodies.Contains(a) && bodies.Find(c)->mass == 30.0f && bodies.Size() == 2,
                "components::remove_keeps_the_others");
            report.Check(!bodies.Remove(a), "components::remove_missing");

            report.Check(scene.RemoveNode(b) && !bodies.Contains(b) && bodies.Size() == 1, "components::go_with_their_node");
            const NodeId d = scene.AddNode({ .name = "d" });
            report.Check(d.Index() == b.Index() && !bodies.Contains(d) && !bodies.Find(b), "components::reused_slot_starts_empty");

            // A node that went away without the store hearing of it: its slot's next node replaces the entry
            ComponentStore<Health> store;
            const NodeId old(5, 1);
            const NodeId other(6, 1);
            const NodeId fresh(5, 2);
            store.Emplace(old, 1);
            store.Emplace(other, 2);
            store.Emplace(fresh, 3);
            report.Check(store.Size() == 2 && !store.Contains(old) && store.Find(fresh)->value == 3 && store.Find(other)->value == 2,
                "components::stale_entry_is_replaced");
            store.Remove(other);
            report.Check(store.Size() == 1 && store.Find(fresh) && store.Find(fresh)->value == 3, "components::removal_after_stale_entry");

            scene.Clear();
            report.Check(bodies.Empty(), "components::clear_with_the_scene");

            bool namesRoundTrip = !ComponentTypeFromName("Rigidbody");
            for (const ComponentType type : AllComponentTypes)
                namesRoundTrip = namesRoundTrip && ComponentTypeFromName(ComponentTypeName(type)) == type;
            report.Check(namesRoundTrip, "components::type_names_round_trip");

            // Components move over to the ids remap gives; the ones without a node are dropped
            ComponentStores source;
            source.Components<Collider>().Emplace(SceneAsset::NodeRef(0), Collider { .radius = 2.0f });
            source.Components<Collider>().Emplace(SceneAsset::NodeRef(1), Collider { .radius = 3.0f });
            const NodeId target = scene.AddNode({ .name = "target" });
            ComponentStores moved;
            moved.TakeFrom(std::move(source), [&](NodeId key) { return key == SceneAsset::NodeRef(0) ? target : NodeId { }; });
            const auto* colliders = moved.FindComponents<Collider>();
            report.Check(colliders && colliders->Size() == 1 && colliders->Find(target) && colliders->Find(target)->radius == 2.0f,
                "components::take_from_remaps_ids");
        }

        // ── Hot reload and console ──────────────────────────────────────────────

        void WriteScript(const std::filesystem::path& path, const char* text)
        {
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }

        /// Moves the file time forward: two writes within the clock resolution look unchanged.
        void Touch(const std::filesystem::path& path, int seconds)
        {
            std::error_code error;
            std::filesystem::last_write_time(path, std::filesystem::last_write_time(path, error) + std::chrono::seconds(seconds), error);
        }

        String Evaluate(ScriptSystem& system, const char* code)
        {
            auto result = SelfTestAccess::Console(system).Execute(code);
            return result ? *result : "error: " + result.error().message;
        }

        const LoadedModule* Loaded(ScriptSystem& system, StringView name)
        {
            for (const auto& module : SelfTestAccess::Loader(system).LoadedModules()) {
                if (module.name == name)
                    return &module;
            }
            return nullptr;
        }

        void RunHotReloadTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_script_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root, error);
            const fs::path hot = root / "hot.lua";
            const fs::path mainFile = root / "main.lua";

            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            ScriptSystem system;
            if (!system.Init({ nullptr, nullptr, nullptr, nullptr, &settings }, Path(root.generic_string().c_str()))) {
                report.Check(false, "hot::init");
                return;
            }
            // The main module's on_update runs in Play mode only
            system.OnPlay();
            lua_State* L = SelfTestAccess::State(system);

            // ── Module reload ──
            WriteScript(hot, "local M = {} M.value = 1 function M.get() return M.value end return M");
            report.Check(Evaluate(system, "hot = require 'hot' return hot.get()") == "1", "hot::module_loads");

            WriteScript(hot, "local M = {} M.value = 2 function M.get() return M.value * 10 end return M");
            Touch(hot, 2);
            const bool reloaded = SelfTestAccess::Reload(system).Reload("hot").has_value();
            report.Check(reloaded && Evaluate(system, "hot.get()") == "20", "hot::reload_updates_the_held_table");
            report.Check(Evaluate(system, "rawequal(hot, require 'hot')") == "true", "hot::module_keeps_its_identity");

            WriteScript(hot, "local M = {} M.value = = 3 return M");
            report.Check(!SelfTestAccess::Reload(system).Reload("hot") && !Loaded(system, "hot")->error.empty(), "hot::syntax_error_is_reported");
            report.Check(Evaluate(system, "hot.get()") == "20", "hot::failed_reload_keeps_the_old_version");

            WriteScript(hot, "local M = {} M.value = 3 function M.on_reload(previous) M.carried = previous.value end return M");
            report.Check(SelfTestAccess::Reload(system).Reload("hot").has_value() && Loaded(system, "hot")->error.empty(), "hot::fixed_module_reloads");
            report.Check(Evaluate(system, "hot.carried") == "2", "hot::on_reload_gets_the_previous_fields");
            report.Check(Evaluate(system, "hot.get") == "nil", "hot::removed_fields_disappear");

            WriteScript(hot, "local M = { counter = 0 } function M.inc() M.counter = M.counter + 1 end return M");
            (void)SelfTestAccess::Reload(system).Reload("hot");
            report.Check(Evaluate(system, "hot.inc() hot.inc() return hot.counter") == "2",
                "hot::module_code_sees_the_held_table");

            WriteScript(hot, "local state = elm.script.persist('hot', { count = 0 }) state.count = state.count + 1 return { count = function() return state.count end }");
            (void)SelfTestAccess::Reload(system).Reload("hot");
            (void)SelfTestAccess::Reload(system).Reload("hot");
            report.Check(Evaluate(system, "hot.count()") == "2", "hot::persist_survives_reloads");

            WriteScript(hot, "local M = {} M.value = 4 return M");
            Touch(hot, 4);
            report.Check(SelfTestAccess::Reload(system).ReloadChanged() == 1 && Evaluate(system, "hot.value") == "4", "hot::reload_changed_finds_the_file");
            report.Check(SelfTestAccess::Reload(system).ReloadChanged() == 0, "hot::reload_changed_skips_unchanged");

            // Polling: the change is applied once two polls see the same file time
            WriteScript(hot, "local M = {} M.value = 5 return M");
            Touch(hot, 6);
            for (int poll = 0; poll < 3; ++poll) {
                SelfTestAccess::Reload(system).Poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
            }
            report.Check(Evaluate(system, "hot.value") == "5", "hot::poll_reloads_changed_files");

            // ── Main module ──
            WriteScript(mainFile, "local main = {} function main.on_start() started = (started or 0) + 1 end return main =");
            report.Check(!system.Start(settings) && SelfTestAccess::Main(system).GetState() == MainState::Idle
                    && Loaded(system, "main") && !Loaded(system, "main")->error.empty(),
                "hot::broken_main_is_idle");
            WriteScript(mainFile,
                "local main = {}\n"
                "function main.on_start() started = (started or 0) + 1 end\n"
                "function main.on_update(dt) if fail then error('update failed') end ticks = (ticks or 0) + 1 end\n"
                "return main\n");
            report.Check(SelfTestAccess::Reload(system).Reload("main").has_value() && SelfTestAccess::Main(system).GetState() == MainState::Running
                    && Evaluate(system, "started") == "1",
                "hot::fixed_main_starts");

            (void)SelfTestAccess::Console(system).Execute("fail = true");
            system.Update(0.016f);
            report.Check(SelfTestAccess::Main(system).GetState() == MainState::Faulted
                    && SelfTestAccess::Main(system).Error().find("update failed") != String::npos,
                "hot::failing_hook_faults_the_main_module");
            (void)SelfTestAccess::Console(system).Execute("fail = false");
            report.Check(SelfTestAccess::Reload(system).Reload("main").has_value() && SelfTestAccess::Main(system).GetState() == MainState::Running,
                "hot::reload_recovers_a_faulted_main");
            system.Update(0.016f);
            report.Check(Evaluate(system, "ticks") == "1" && Evaluate(system, "started") == "1", "hot::reload_does_not_restart_main");

            // ── Console ──
            report.Check(Evaluate(system, "1 + 2") == "3", "console::expression_value");
            report.Check(Evaluate(system, "x = 5") == "" && Evaluate(system, "x") == "5", "console::statement");
            report.Check(Evaluate(system, "print('hi') return 7, 'a'") == "hi\n7\ta", "console::print_and_results");
            report.Check(Evaluate(system, "error('bad thing')").find("bad thing") != String::npos, "console::runtime_error");
            report.Check(Evaluate(system, "1 +").starts_with("error: "), "console::syntax_error");
            report.Check(Evaluate(system, "setmetatable({}, { __tostring = function() error('no') end })").starts_with("error: "),
                "console::failing_tostring_is_an_error");

            auto has = [](const Vector<String>& list, StringView item) {
                return std::find(list.begin(), list.end(), item) != list.end();
            };
            const auto modules = SelfTestAccess::Console(system).Complete("elm.sc");
            report.Check(has(modules, "elm.scene") && has(modules, "elm.script"), "console::complete_module");
            report.Check(has(SelfTestAccess::Console(system).Complete("x = elm.math.Vector3.u"), "x = elm.math.Vector3.up"), "console::complete_in_expression");
            (void)SelfTestAccess::Console(system).Execute("vv = elm.math.Vector3(1, 2, 3)");
            report.Check(has(SelfTestAccess::Console(system).Complete("vv:norm"), "vv:normalized") && has(SelfTestAccess::Console(system).Complete("vv.le"), "vv.length")
                    && !has(SelfTestAccess::Console(system).Complete("vv:le"), "vv:length"),
                "console::complete_userdata_members");
            report.Check(SelfTestAccess::Console(system).Complete("nothing.here.").empty(), "console::complete_unknown_is_empty");
            report.Check(lua_gettop(L) == 0, "console::stack_balanced");

            // ── Module names and main functions ──
            const Path scriptRoot(root.generic_string().c_str());
            report.Check(SelfTestAccess::Paths(system).NameOf(scriptRoot / "hot.lua") == "hot"
                    && SelfTestAccess::Paths(system).NameOf(scriptRoot / "a/b.lua") == "a.b"
                    && SelfTestAccess::Paths(system).NameOf(scriptRoot / "pkg/init.lua") == "pkg"
                    && SelfTestAccess::Paths(system).NameOf(scriptRoot / "notes.txt").empty()
                    && SelfTestAccess::Paths(system).NameOf(scriptRoot / "../outside.lua").empty(),
                "hot::module_name_of_file");
            WriteScript(mainFile,
                "local main = {}\n"
                "function main.build_scene() built = (built or 0) + 1 end\n"
                "function main.broken() error('build failed') end\n"
                "return main\n");
            (void)SelfTestAccess::Reload(system).Reload("main");
            const auto built = SelfTestAccess::Main(system).CallFunction("build_scene");
            const auto missing = SelfTestAccess::Main(system).CallFunction("missing");
            const auto broken = SelfTestAccess::Main(system).CallFunction("broken");
            report.Check(built && *built && Evaluate(system, "built") == "1" && missing && !*missing
                    && !broken && broken.error().message.find("build failed") != String::npos
                    && SelfTestAccess::Main(system).GetState() == MainState::Running,
                "hot::call_main_function");

            // ── Error locations for the editor ──
            {
                const auto syntax = ScriptEditor::FindLocation("demo/spinner.lua:12: unexpected symbol near '='");
                const auto loading = ScriptEditor::FindLocation("error loading module 'main':\n\tmain.lua:3: <eof> expected");
                const auto runtime = ScriptEditor::FindLocation("on_update: [string \"x\"]:1: oops\nstack traceback:\n\tgame/player.lua:40: in function");
                report.Check(syntax && syntax->file == "demo/spinner.lua" && syntax->line == 12
                        && loading && loading->file == "main.lua" && loading->line == 3
                        && runtime && runtime->file == "game/player.lua" && runtime->line == 40
                        && !ScriptEditor::FindLocation("no location here.lua: at all"),
                    "editor::error_location");
            }

            // ── Restart ──
            SelfTestAccess::RequestRestart(system);
            system.Update(0.016f);
            report.Check(system.IsInitialized() && SelfTestAccess::Main(system).GetState() == MainState::Running
                    && Evaluate(system, "built") == "nil" && Evaluate(system, "hot") == "nil",
                "hot::restart_starts_a_fresh_state");

            system.Shutdown();
            fs::remove_all(root, error);
        }

        // ── Entity scripts ──────────────────────────────────────────────────────

        /// The test class: counts updates by `step`, logs on_start and on_destroy into the global events.
        String WalkerSource(int step)
        {
            return "local Walker = { props = { speed = 2, label = 'walker', dir = elm.math.Vector3(1, 0, 0) } }\n"
                   "function Walker:on_start() events[#events + 1] = 'start ' .. self.label self.ticks = 0 end\n"
                   "function Walker:on_update(dt) if self.fail then error('walker failed') end self.ticks = self.ticks + "
                + String(std::to_string(step)) + " end\n"
                   "function Walker:on_fixed_update(dt) self.fixed = (self.fixed or 0) + 1 end\n"
                   "function Walker:on_destroy() events[#events + 1] = 'destroy ' .. self.label .. ' ' .. tostring(self.entity.valid) end\n"
                   "function Walker:on_script_reload() self.reloaded = true end\n"
                   "return Walker\n";
        }

        void RunEntityScriptTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_entity_script_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root / "npc", error);
            const fs::path walker = root / "npc" / "walker.lua";
            WriteScript(walker, WalkerSource(1).c_str());
            WriteScript(root / "broken.lua", "return 42");
            // Started by the restart below: the fresh state needs the events global
            WriteScript(root / "main.lua", "events = {} return {}");

            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            Scene scene;
            ScriptSystem system;
            if (!system.Init({ &scene, nullptr, nullptr, nullptr, &settings }, Path(root.generic_string().c_str()))) {
                report.Check(false, "entity::init");
                return;
            }
            system.OnPlay();
            auto eval = [&](const char* code) { return Evaluate(system, code); };
            auto fails = [&](const char* code, const char* text) {
                const String result = eval(code);
                return result.starts_with("error: ") && result.find(text) != String::npos;
            };
            (void)SelfTestAccess::Console(system).Execute("events = {}");

            // ── Lifecycle ──
            report.Check(eval("a = elm.scene.spawn('a') wa = a:add('Script', { module = 'npc.walker', props = { label = 'a', speed = 5 } }) "
                              "return wa.speed, wa.label, wa.entity == a, wa.ticks, a.script == wa, a:has('Script')")
                    == "5\ta\ttrue\tnil\ttrue\ttrue",
                "entity::add_makes_the_instance");
            report.Check(eval("return tostring(wa)") == "Script(npc.walker, Entity(a))", "entity::instance_tostring");
            report.Check(eval("b = elm.scene.spawn('b') wb = b:add('Script', 'npc.walker') return wb.speed, wb.label") == "2\twalker",
                "entity::props_default_from_the_class");
            report.Check(eval("wa.dir.x = 9 return wb.dir.x") == "1.0", "entity::vector_props_are_not_shared");

            system.Update(0.016f);
            report.Check(eval("return events[1], events[2], wa.ticks, wb.ticks") == "start a\tstart walker\t1\t1",
                "entity::on_start_then_on_update");
            system.FixedUpdate(1.0f / 60.0f);
            report.Check(eval("return wa.fixed") == "1", "entity::on_fixed_update");

            // A component added without Lua (scene file, C++) gets its instance on the next update
            const NodeId nodeA = scene.IdOf(0);
            const NodeId nodeC = scene.AddNode({ .name = "c" });
            scene.Components<ScriptComponent>().Emplace(nodeC, ScriptComponent { "npc.walker", { { "label", ScriptValue(String("c")) } } });
            system.Update(0.016f);
            report.Check(eval("c = elm.scene.find('c') return c.script.label, c.script.ticks, #elm.scene.with('Script')") == "c\t1\t3",
                "entity::component_from_cpp_starts");

            // ── Errors and reload ──
            (void)SelfTestAccess::Console(system).Execute("wa.fail = true");
            system.Update(0.016f);
            const auto* faulted = SelfTestAccess::Entities(system).Find(nodeA);
            report.Check(faulted && faulted->faulted && faulted->error.find("walker failed") != String::npos
                    && eval("return wa.ticks, wb.ticks") == "2\t3",
                "entity::error_stops_only_that_instance");

            (void)SelfTestAccess::Console(system).Execute("wa.fail = false");
            WriteScript(walker, WalkerSource(10).c_str());
            report.Check(SelfTestAccess::Reload(system).Reload("npc.walker").has_value() && !SelfTestAccess::Entities(system).Find(nodeA)->faulted,
                "entity::reload_clears_the_fault");
            system.Update(0.016f);
            report.Check(eval("return wa.ticks, wb.ticks, wa.reloaded") == "12\t13\ttrue", "entity::reload_patches_the_instances");

            // ── Ending ──
            (void)SelfTestAccess::Console(system).Execute("events = {} a:destroy()");
            report.Check(eval("return events[1], wa.entity.valid, a.valid") == "destroy a true\tfalse\tfalse",
                "entity::destroy_calls_on_destroy_first");
            // Removed behind the scripts' back (editor, C++): on_destroy on the next pass, the entity is gone already
            scene.RemoveNode(nodeC);
            system.Update(0.016f);
            report.Check(eval("return events[2]") == "destroy c false", "entity::removed_node_is_noticed");
            report.Check(eval("return b:remove('Script'), b:has('Script'), b.script, events[3], b.valid")
                    == "true\tfalse\tnil\tdestroy walker true\ttrue",
                "entity::remove_component");
            report.Check(eval("return b:remove('Script')") == "false", "entity::remove_missing_component");
            report.Check(eval("events = {} d = elm.scene.spawn('d') d:add('Script', 'npc.walker') d:destroy() return #events") == "0",
                "entity::no_on_destroy_without_on_start");
            // on_destroy that destroys its own entity: the instance ends once, the entity goes
            WriteScript(root / "npc" / "selfish.lua",
                "local Selfish = {}\n"
                "function Selfish:on_start() end\n"
                "function Selfish:on_destroy() events[#events + 1] = 'selfish' self.entity:destroy() end\n"
                "return Selfish\n");
            (void)SelfTestAccess::Console(system).Execute("events = {} s = elm.scene.spawn('s') s:add('Script', 'npc.selfish') "
                                 "t = elm.scene.spawn('t') t:add('Script', 'npc.selfish')");
            system.Update(0.016f);
            report.Check(eval("s:destroy() t:remove('Script') return #events, s.valid, t.valid") == "2\tfalse\tfalse",
                "entity::on_destroy_may_destroy_its_entity");
            report.Check(SelfTestAccess::Entities(system).All().empty(), "entity::ended_instances_are_dropped");

            // ── Bad input ──
            (void)SelfTestAccess::Console(system).Execute("e = elm.scene.spawn('e')");
            report.Check(fails("e:add('Script', 'npc.missing')", "module 'npc.missing' not found") && eval("return e:has('Script')") == "false",
                "entity::missing_module_fails_at_add");
            report.Check(fails("e:add('Script', 'broken')", "an entity script returns its class table"), "entity::module_returns_a_table");
            report.Check(fails("e:add('Script', { module = 'npc.walker', props = { speed = 'fast' } })", "props.speed: number expected, got string"),
                "entity::prop_type_is_checked");
            report.Check(fails("e:add('Script', { module = 'npc.walker', props = { list = {} } })", "props.list: table cannot be a prop"),
                "entity::prop_types_are_limited");
            report.Check(fails("e:add('Script', { modul = 'npc.walker' })", "no field 'modul'"), "entity::init_typo");
            report.Check(fails("e:add('Rigidbody', {})", "unknown component type 'Rigidbody'"), "entity::unknown_component");
            report.Check(eval("e:add('Script', 'npc.walker') return e:has('Script')") == "true"
                    && fails("e:add('Script', 'npc.walker')", "already has a Script"),
                "entity::one_script_per_entity");

            // A component naming a missing module is kept faulted, a reload makes it again
            const NodeId nodeF = scene.AddNode({ .name = "f" });
            scene.Components<ScriptComponent>().Emplace(nodeF, ScriptComponent { "npc.later", { } });
            system.Update(0.016f);
            const auto* broken = SelfTestAccess::Entities(system).Find(nodeF);
            report.Check(broken && broken->faulted && !broken->self && fails("return elm.scene.find('f').script", "npc.later"),
                "entity::broken_component_stays_faulted");
            WriteScript(root / "npc" / "later.lua",
                "local Later = {}\n"
                "function Later:on_start() events[#events + 1] = 'later' end\n"
                "function Later:on_destroy() events[#events + 1] = 'later destroyed' end\n"
                "return Later\n");
            (void)SelfTestAccess::Reload(system).Reload("npc.walker");
            system.Update(0.016f);
            report.Check(eval("return events[#events]") == "later", "entity::reload_retries_broken_components");

            // ── Scene commands and restart ──
            SelfTestAccess::RequestRestart(system);
            system.Update(0.016f);
            report.Check(SelfTestAccess::Entities(system).All().size() == 2 && eval("table.sort(events) return table.concat(events, ',')") == "later,start walker",
                "entity::restart_makes_the_instances_again");
            report.Check(eval("events = {} elm.engine.clear_scene() table.sort(events) return table.concat(events, ','), elm.scene.count()")
                    == "destroy walker true,later destroyed\t0",
                "entity::clear_scene_ends_the_scripts_first");
            system.Update(0.016f);
            report.Check(SelfTestAccess::Entities(system).All().empty() && lua_gettop(SelfTestAccess::State(system)) == 0, "entity::nothing_left");

            system.Shutdown();
            fs::remove_all(root, error);
        }

        void RunEntityScriptListTests(Report& report)
        {
            EntityScriptList list;
            const NodeId a(1, 1);
            const NodeId b(2, 1);
            const size_t first = list.Add(EntityScript { .node = a, .module = "m" });
            (void)list.Add(EntityScript { .node = b, .module = "m" });
            report.Check(first == 0 && list.FindIndex(a) == 0 && list.FindIndex(b) == 1 && list[0].serial != list[1].serial,
                "entity_list::add_and_find");
            {
                EntityScriptList::Pass pass(list);
                list.MarkDead(0);
                list.DropDead();
                report.Check(list.Size() == 2 && list.FindIndex(a) == EntityScriptList::NoIndex && list.IsPassRunning(),
                    "entity_list::dead_entries_stay_while_a_pass_runs");
            }
            list.DropDead();
            report.Check(list.Size() == 1 && list.FindIndex(b) == 0 && !list.IsPassRunning(), "entity_list::drop_dead_after_the_pass");
            const uint64_t earlier = list[0].serial;
            const size_t again = list.Add(EntityScript { .node = a, .module = "m" });
            report.Check(list.FindIndex(a) == again && list[again].serial > earlier, "entity_list::new_instance_new_serial");
        }

        // ── Physics ─────────────────────────────────────────────────────────────

        NodeId Place(Scene& scene, const char* name, const Vector3& position)
        {
            return scene.AddNode({ .name = name, .localTransform = Matrix4x4::Translation(position) });
        }

        Vector3 WorldPosition(Scene& scene, NodeId node)
        {
            const Matrix4x4 m = WorldOf(scene, node);
            return { m(0, 3), m(1, 3), m(2, 3) };
        }

        bool IsPair(const CollisionEvent& event, NodeId a, NodeId b)
        {
            return (event.first == a && event.second == b) || (event.first == b && event.second == a);
        }

        void RunPhysicsTests(Report& report)
        {
            PhysicsSystem physics;
            if (!physics.Init()) {
                report.Check(false, "physics::init");
                return;
            }
            constexpr float Step = 1.0f / 60.0f;
            Scene scene;
            auto& colliders = scene.Components<Collider>();
            auto& bodies = scene.Components<RigidBody>();

            const NodeId ground = Place(scene, "ground", { 0.0f, -0.5f, 0.0f });
            colliders.Emplace(ground, Collider { .halfExtents = { 10.0f, 0.5f, 10.0f } });
            const NodeId box = Place(scene, "box", { 0.0f, 3.0f, 0.0f });
            colliders.Emplace(box);
            bodies.Emplace(box, RigidBody { .mass = 2.0f });
            physics.Step(scene, Step);
            report.Check(physics.GetNumBodies() == 2 && physics.HasBody(ground) && physics.HasBody(box), "physics::colliders_make_bodies");

            bool landed = false;
            for (int frame = 0; frame < 180; ++frame) {
                physics.Step(scene, Step);
                for (const auto& event : physics.CollisionEvents()) {
                    // The normal points from first to second: up when the ground comes first
                    const float up = event.first == ground ? event.normal.y : -event.normal.y;
                    landed |= event.kind == CollisionEvent::Kind::Enter && IsPair(event, box, ground) && !event.trigger && up > 0.9f;
                }
            }
            const float restingY = WorldPosition(scene, box).y;
            report.Check(std::abs(restingY - 0.5f) < 0.05f, "physics::dynamic_body_falls_and_rests", "y: " + std::to_string(restingY));
            report.Check(landed, "physics::collision_enter_event");

            scene.SetLocalTransform(box, Matrix4x4::Translation(Vector3 { 5.0f, 3.0f, 0.0f }));
            physics.Step(scene, Step);
            report.Check(std::abs(WorldPosition(scene, box).x - 5.0f) < 0.01f, "physics::moved_node_teleports_the_body");

            // A kinematic body moves to its node with the velocity that takes it there in one step
            const NodeId mover = Place(scene, "mover", { -5.0f, 1.0f, 0.0f });
            colliders.Emplace(mover);
            bodies.Emplace(mover, RigidBody { .type = BodyType::Kinematic });
            physics.Step(scene, Step);
            scene.SetLocalTransform(mover, Matrix4x4::Translation(Vector3 { -4.0f, 1.0f, 0.0f }));
            physics.Step(scene, Step);
            const auto moving = physics.GetLinearVelocity(mover);
            report.Check(moving && std::abs(moving->x - 60.0f) < 0.5f && std::abs(WorldPosition(scene, mover).x + 4.0f) < 1e-3f,
                "physics::kinematic_body_follows_its_node");
            physics.Step(scene, Step);
            const auto stopped = physics.GetLinearVelocity(mover);
            report.Check(stopped && stopped->Length() < 1e-3f, "physics::kinematic_body_stops_with_its_node");

            // A trigger reports what passes through it and stops nothing
            const NodeId zone = Place(scene, "zone", { -8.0f, 6.0f, 0.0f });
            colliders.Emplace(zone, Collider { .halfExtents = { 1.0f, 1.0f, 1.0f }, .trigger = true });
            const NodeId ball = Place(scene, "ball", { -8.0f, 9.0f, 0.0f });
            colliders.Emplace(ball, Collider { .shape = ColliderShape::Sphere, .radius = 0.25f });
            bodies.Emplace(ball);
            bool entered = false;
            bool left = false;
            for (int frame = 0; frame < 90; ++frame) {
                physics.Step(scene, Step);
                for (const auto& event : physics.CollisionEvents()) {
                    entered |= IsPair(event, zone, ball) && event.trigger && event.kind == CollisionEvent::Kind::Enter;
                    left |= IsPair(event, zone, ball) && event.trigger && event.kind == CollisionEvent::Kind::Exit && entered;
                }
            }
            report.Check(entered && left && WorldPosition(scene, ball).y < 4.0f, "physics::trigger_enter_and_exit");

            // A changed component builds the body again: the new mass shows in the answer to an impulse
            bodies.Find(box)->mass = 10.0f;
            physics.Step(scene, Step);
            const auto before = physics.GetLinearVelocity(box);
            physics.AddImpulse(box, { 10.0f, 0.0f, 0.0f });
            const auto after = physics.GetLinearVelocity(box);
            report.Check(before && after && std::abs(after->x - before->x - 1.0f) < 0.01f, "physics::changed_component_rebuilds_the_body");

            const auto down = physics.CastRay({ -2.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 10.0f);
            report.Check(down && down->node == ground && std::abs(down->distance - 5.0f) < 0.01f && down->normal.y > 0.99f,
                "physics::raycast_hits_the_ground");
            report.Check(!physics.CastRay({ -2.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 10.0f, ground), "physics::raycast_ignores_a_body");
            const auto throughTrigger = physics.CastRay({ -8.0f, 7.5f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 10.0f);
            report.Check(throughTrigger && throughTrigger->node == ball, "physics::raycast_skips_triggers");

            colliders.Remove(mover);
            scene.RemoveNode(ball);
            physics.Step(scene, Step);
            report.Check(!physics.HasBody(mover) && !physics.HasBody(ball) && physics.GetNumBodies() == 3,
                "physics::bodies_go_with_their_component_or_node");
            scene.Clear();
            physics.Step(scene, Step);
            report.Check(physics.GetNumBodies() == 0, "physics::cleared_scene_has_no_bodies");
            physics.Shutdown();
        }

        /// The Lua side: components, body access, collision and trigger hooks of entity scripts.
        void RunPhysicsScriptTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_physics_script_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root, error);
            WriteScript(root / "ball.lua",
                "local Ball = {}\n"
                "function Ball:on_collision_enter(other, contact) hits[#hits + 1] = other.name .. (contact.normal.y < -0.5 and ' below' or '') end\n"
                "return Ball\n");
            WriteScript(root / "pad.lua",
                "local Pad = { props = { force = 8 } }\n"
                "function Pad:on_trigger_enter(other)\n"
                "    triggers[#triggers + 1] = 'pad hit by ' .. other.name\n"
                "    other.rigidbody:add_impulse(elm.math.Vector3(0, self.force, 0))\n"
                "    launched = launched or other.rigidbody.velocity.y\n"
                "end\n"
                "function Pad:on_trigger_exit(other) triggers[#triggers + 1] = 'left ' .. other.name end\n"
                "return Pad\n");

            PhysicsSystem physics;
            if (!physics.Init()) {
                report.Check(false, "physics_lua::init");
                return;
            }
            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            Scene scene;
            ScriptSystem system;
            if (!system.Init({ &scene, nullptr, nullptr, nullptr, &settings, &physics }, Path(root.generic_string().c_str()))) {
                report.Check(false, "physics_lua::init");
                return;
            }
            system.OnPlay();
            auto eval = [&](const char* code) { return Evaluate(system, code); };
            auto fails = [&](const char* code, const char* text) {
                const String result = eval(code);
                return result.starts_with("error: ") && result.find(text) != String::npos;
            };
            constexpr float Step = 1.0f / 60.0f;
            auto run = [&](int frames) {
                for (int frame = 0; frame < frames; ++frame) {
                    physics.Step(scene, Step);
                    system.FixedUpdate(Step, physics.CollisionEvents());
                    system.Update(Step);
                }
            };
            (void)SelfTestAccess::Console(system).Execute("hits = {} triggers = {} Vector3 = elm.math.Vector3");

            report.Check(eval("ground = elm.scene.spawn('ground') ground.position = Vector3(0, -0.5, 0) "
                              "local c = ground:add('Collider', { half_extents = Vector3(10, 0.5, 10) }) "
                              "return tostring(c), c.shape, c.half_extents.x, ground:has('Collider'), ground.rigidbody")
                    == "Collider(box, ground)\tbox\t10.0\ttrue\tnil",
                "physics_lua::static_collider");
            report.Check(eval("ball = elm.scene.spawn('ball') ball.position = Vector3(0, 3, 0) "
                              "ball:add('Collider', { shape = 'sphere', radius = 0.25 }) "
                              "rb = ball:add('RigidBody', { mass = 2 }) ball:add('Script', 'ball') "
                              "return tostring(rb), rb.mass, rb.type, ball.rigidbody == rb, #elm.scene.with('Collider')")
                    == "RigidBody(dynamic, ball)\t2.0\tdynamic\ttrue\t2",
                "physics_lua::dynamic_body");
            report.Check(eval("rb.velocity = Vector3(1, 0, 0) return rb.velocity.x") == "1.0", "physics_lua::body_is_there_right_after_add");

            report.Check(fails("rb.type = 'floating'", "unknown body type 'floating'"), "physics_lua::body_type_checked");
            report.Check(fails("rb.mass = 0", "mass must be at least 0.001, got 0.0") && fails("rb.restitution = 2", "restitution must be in 0.0..1.0"),
                "physics_lua::values_checked", eval("rb.mass = 0"));
            report.Check(fails("elm.scene.spawn('x'):add('RigidBody', { masss = 1 })", "no writable field 'masss'")
                    && eval("return elm.scene.find('x'):has('RigidBody')") == "false",
                "physics_lua::bad_init_field_adds_nothing");
            report.Check(fails("elm.scene.find('x'):add('Collider', { shape = 'cone' })", "unknown collider shape 'cone'"), "physics_lua::shape_checked");
            report.Check(fails("return elm.scene.find('x'):add('RigidBody').velocity", "needs a Collider"), "physics_lua::body_needs_a_collider");
            report.Check(fails("k = elm.scene.spawn('k') k.position = Vector3(-6, 3, 0) k:add('Collider') "
                               "k:add('RigidBody', { type = 'kinematic' }):add_impulse(Vector3(1, 0, 0))",
                             "add_impulse needs a dynamic body, this one is kinematic"),
                "physics_lua::forces_need_a_dynamic_body");
            (void)SelfTestAccess::Console(system).Execute("elm.scene.find('x'):destroy() k:destroy()");

            // A launch pad: a trigger whose script throws what enters it upwards
            report.Check(eval("pad = elm.scene.spawn('pad') pad.position = Vector3(3, 0.5, 0) "
                              "pad:add('Collider', { half_extents = Vector3(1, 0.5, 1), trigger = true }) "
                              "pad:add('Script', { module = 'pad', props = { force = 12 } }) "
                              "crate = elm.scene.spawn('crate') crate.position = Vector3(3, 2.5, 0) "
                              "crate:add('Collider') crate:add('RigidBody') return pad.collider.trigger")
                    == "true",
                "physics_lua::trigger_collider");

            run(120);
            report.Check(eval("return hits[1]") == "ground below", "physics_lua::on_collision_enter", eval("return hits[1]"));
            const String launches = eval("return table.concat(triggers, ',')");
            report.Check(launches.find("pad hit by crate") != String::npos && launches.find("left crate") != String::npos
                    && eval("return launched > 5") == "true",
                "physics_lua::trigger_hooks_launch_the_crate", launches);

            report.Check(eval("local e, p, n, d = elm.physics.raycast(Vector3(-5, 5, 0), Vector3(0, -1, 0)) "
                              "return e.name, math.floor(d + 0.5), n.y > 0.99")
                    == "ground\t5\ttrue",
                "physics_lua::raycast");
            report.Check(eval("return elm.physics.raycast(Vector3(-5, 5, 0), Vector3(0, -1, 0), 10, ground)") == "nil\tnil\tnil\tnil",
                "physics_lua::raycast_ignore");
            report.Check(eval("ball:remove('RigidBody') return ball:has('RigidBody'), ball:has('Collider'), rb.valid") == "false\ttrue\tfalse",
                "physics_lua::remove_component");

            system.Shutdown();
            physics.Shutdown();
            fs::remove_all(root, error);
        }

        // ── Scene files ─────────────────────────────────────────────────────────

        const ScriptValue* FindProp(const ScriptComponent& script, StringView name)
        {
            for (const auto& [key, value] : script.props) {
                if (key == name)
                    return &value;
            }
            return nullptr;
        }

        template <typename T>
        bool PropIs(const ScriptComponent& script, StringView name, const T& expected)
        {
            const ScriptValue* value = FindProp(script, name);
            const T* item = value ? std::get_if<T>(value) : nullptr;
            return item && *item == expected;
        }

        void RunSceneFileTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_scene_file_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root, error);
            auto pathOf = [&](const char* name) { return Path(String((root / name).generic_string().c_str())); };

            render::RenderResources resources;
            graphics::ModelManager models(resources);
            Scene scene;
            const NodeId guard = Place(scene, "guard", { 1.0f, 0.0f, 2.0f });
            const NodeId target = Place(scene, "target", { 5.0f, 0.0f, 0.0f });
            const NodeId plain = Place(scene, "plain", { });
            const NodeId gone = Place(scene, "gone", { });
            (void)scene.SetParent(target, guard);
            graphics::Model cubeModel("Cube");
            cubeModel.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            const ModelComponent guardModel { .model = models.Insert(cubeModel), .color = { 0.5f, 0.25f, 1.0f, 1.0f }, .visible = false };
            scene.Components<ModelComponent>().Emplace(guard, guardModel);
            // Colored by a script, without a model
            scene.Components<ModelComponent>().Emplace(plain, ModelComponent { .color = { 0.0f, 1.0f, 0.0f, 1.0f } });
            const ScriptComponent script { "npc.guard",
                { { "alarm", ScriptValue(true) }, { "count", ScriptValue(int64_t { 3 }) }, { "label", ScriptValue(String("north")) },
                    { "lost", ScriptValue(gone) }, { "offset", ScriptValue(Vector3 { 1.0f, 2.0f, 3.0f }) }, { "speed", ScriptValue(2.5) },
                    { "target", ScriptValue(target) }, { "tint", ScriptValue(Vector4 { 1.0f, 0.0f, 0.0f, 1.0f }) } } };
            scene.Components<ScriptComponent>().Emplace(guard, script);
            const RigidBody body { .type = BodyType::Kinematic, .mass = 3.0f, .friction = 0.25f, .restitution = 0.5f,
                .linearDamping = 0.1f, .angularDamping = 0.2f, .gravityScale = 0.5f };
            scene.Components<RigidBody>().Emplace(guard, body);
            const Collider capsule { .shape = ColliderShape::Capsule, .halfExtents = { 1.0f, 2.0f, 3.0f }, .radius = 0.4f,
                .halfHeight = 0.9f, .offset = { 0.0f, 1.0f, 0.0f }, .trigger = true };
            scene.Components<Collider>().Emplace(guard, capsule);
            scene.Components<Collider>().Emplace(target);
            // An Entity prop pointing at a node that is gone is saved as no entity
            scene.RemoveNode(gone);
            scene.UpdateWorldTransforms();

            const auto saved = resmgr::Serializer::SaveScene(scene, models, pathOf("components.scene"), pathOf(""));
            report.Check(saved.has_value(), "scene_file::save_with_components", saved ? "" : saved.error().message);
            {
                // Component floats are written in their shortest form, for people who edit the file
                std::ifstream stream(root / "components.scene", std::ios::binary);
                const String text { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
                report.Check(text.find("\"linearDamping\": 0.1") != String::npos && text.find("\"radius\": 0.4") != String::npos
                        && text.find("\"type\": \"kinematic\"") != String::npos && text.find("\"entity\": null") != String::npos,
                    "scene_file::readable_components", text.substr(text.find("\"components\"")));
                // World transforms follow from the local ones and are not written
                report.Check(text.find("worldTransform") == String::npos && text.find("\"Model\"") != String::npos
                        && text.find("\"modelIndex\": 0") != String::npos && text.find("\"formatVersion\": 6") != String::npos,
                    "scene_file::model_is_a_component", text.substr(0, 400));
            }
            auto loaded = resmgr::Serializer::LoadScene(pathOf("components.scene"));
            report.Check(loaded.has_value(), "scene_file::load_with_components", loaded ? "" : loaded.error().message);
            if (!loaded)
                return;

            // Into a scene that has a node already: the loaded nodes get other ids than they had
            Scene restored;
            (void)Place(restored, "existing", { });
            const auto handlers = models.InsertAll(std::move(loaded->models));
            const Vector<NodeId> ids = restored.Append(std::move(*loaded), handlers);
            if (ids.size() != 3) {
                report.Check(false, "scene_file::nodes_round_trip", std::to_string(ids.size()));
                return;
            }
            const NodeId newGuard = ids[0];
            const NodeId newTarget = ids[1];
            const NodeId newPlain = ids[2];
            const Vector3 targetPosition = WorldPosition(restored, newTarget);
            report.Check(restored.Find(newTarget)->parent == newGuard && std::abs(targetPosition.x - 5.0f) < 1e-5f
                    && std::abs(targetPosition.z) < 1e-5f,
                "scene_file::hierarchy_round_trip");
            const auto* restoredModels = restored.FindComponents<ModelComponent>();
            const ModelComponent* newGuardModel = restoredModels ? restoredModels->Find(newGuard) : nullptr;
            const ModelComponent* newPlainModel = restoredModels ? restoredModels->Find(newPlain) : nullptr;
            report.Check(handlers.size() == 1 && newGuardModel && newGuardModel->model == handlers[0] && models.Get(handlers[0])
                    && newGuardModel->color.y == 0.25f && !newGuardModel->visible && newPlainModel && !newPlainModel->model
                    && newPlainModel->color.y == 1.0f && !restoredModels->Contains(newTarget),
                "scene_file::model_component_round_trip");
            const auto* scripts = restored.FindComponents<ScriptComponent>();
            const ScriptComponent* restoredScript = scripts ? scripts->Find(newGuard) : nullptr;
            const ScriptValue* tint = restoredScript ? FindProp(*restoredScript, "tint") : nullptr;
            const auto* tintValue = tint ? std::get_if<Vector4>(tint) : nullptr;
            report.Check(restoredScript && restoredScript->module == "npc.guard" && restoredScript->props.size() == script.props.size()
                    && PropIs(*restoredScript, "alarm", true) && PropIs(*restoredScript, "count", int64_t { 3 })
                    && PropIs(*restoredScript, "speed", 2.5) && PropIs(*restoredScript, "label", String("north"))
                    && PropIs(*restoredScript, "offset", Vector3 { 1.0f, 2.0f, 3.0f })
                    && tintValue && tintValue->x == 1.0f && tintValue->y == 0.0f && tintValue->z == 0.0f && tintValue->w == 1.0f,
                "scene_file::script_props_round_trip");
            report.Check(restoredScript && newTarget != target && PropIs(*restoredScript, "target", newTarget)
                    && PropIs(*restoredScript, "lost", NodeId { }),
                "scene_file::entity_props_follow_the_new_ids");
            const auto* bodies = restored.FindComponents<RigidBody>();
            const auto* colliders = restored.FindComponents<Collider>();
            report.Check(bodies && bodies->Find(newGuard) && *bodies->Find(newGuard) == body && !bodies->Contains(newTarget)
                    && colliders && colliders->Find(newGuard) && *colliders->Find(newGuard) == capsule && colliders->Find(newTarget)
                    && *colliders->Find(newTarget) == Collider { } && !colliders->Contains(newPlain) && !scripts->Contains(newPlain),
                "scene_file::physics_components_round_trip");

            // Hand-written files
            auto writeScene = [&](const char* name, int version, const char* components) {
                const String text = "{ \"formatVersion\": " + String(std::to_string(version)) + ", \"models\": [], \"nodes\": [ { \"name\": \"a\", "
                    "\"localTransform\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], \"worldTransform\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], "
                    "\"visible\": true, \"color\": [1,1,1,1]" + components + " } ] }";
                WriteScript(root / name, text.c_str());
                return pathOf(name);
            };
            const auto old = resmgr::Serializer::LoadScene(writeScene("old.scene", 4, ""));
            report.Check(old && old->nodes.size() == 1 && !old->components.FindComponents<ScriptComponent>()
                    && !old->components.FindComponents<Collider>() && !old->components.FindComponents<ModelComponent>(),
                "scene_file::version_4_still_loads");
            // Before version 6 the model, color and visible were fields of the node; the model file is the one saved above
            const String legacyText = "{ \"formatVersion\": 5, \"models\": [ { \"path\": \"Cube.model\" } ], \"nodes\": [ { \"name\": \"a\", "
                "\"localTransform\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], \"worldTransform\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], "
                "\"visible\": false, \"color\": [0,0,1,1], \"modelIndex\": 0 } ] }";
            WriteScript(root / "legacy.scene", legacyText.c_str());
            const auto legacy = resmgr::Serializer::LoadScene(pathOf("legacy.scene"));
            const auto* legacyModels = legacy ? legacy->components.FindComponents<ModelComponent>() : nullptr;
            const ModelComponent* legacyModel = legacyModels ? legacyModels->Find(SceneAsset::NodeRef(0)) : nullptr;
            report.Check(legacyModel && legacyModel->model == SceneAsset::ModelRef(0) && legacyModel->color.z == 1.0f
                    && !legacyModel->visible && legacy->models.size() == 1,
                "scene_file::version_5_node_model_becomes_a_component", legacy ? "" : legacy.error().message);
            report.Check(!resmgr::Serializer::LoadScene(writeScene("model.scene", 6, R"(, "components": { "Model": { "modelIndex": 0 } })")),
                "scene_file::missing_model_is_an_error");
            const auto defaults = resmgr::Serializer::LoadScene(
                writeScene("defaults.scene", 5, R"(, "components": { "RigidBody": { "mass": 4 }, "Collider": { "shape": "sphere" } })"));
            const auto* loadedBodies = defaults ? defaults->components.FindComponents<RigidBody>() : nullptr;
            const auto* loadedColliders = defaults ? defaults->components.FindComponents<Collider>() : nullptr;
            const RigidBody* loadedBody = loadedBodies ? loadedBodies->Find(SceneAsset::NodeRef(0)) : nullptr;
            const Collider* loadedCollider = loadedColliders ? loadedColliders->Find(SceneAsset::NodeRef(0)) : nullptr;
            report.Check(loadedBody && loadedBody->mass == 4.0f && loadedBody->type == BodyType::Dynamic
                    && loadedCollider && loadedCollider->shape == ColliderShape::Sphere && loadedCollider->radius == 0.5f,
                "scene_file::missing_fields_take_defaults");
            report.Check(!resmgr::Serializer::LoadScene(writeScene("unknown.scene", 5, R"(, "components": { "Rigidbody": { } })")),
                "scene_file::unknown_component_is_an_error");
            report.Check(!resmgr::Serializer::LoadScene(
                             writeScene("prop.scene", 5, R"(, "components": { "Script": { "module": "m", "props": { "x": [1, 2] } } })")),
                "scene_file::bad_prop_is_an_error");
            report.Check(!resmgr::Serializer::LoadScene(
                             writeScene("reference.scene", 5, R"(, "components": { "Script": { "module": "m", "props": { "x": { "entity": 7 } } } })")),
                "scene_file::entity_prop_out_of_range_is_an_error");
            report.Check(!resmgr::Serializer::LoadScene(writeScene("type.scene", 5, R"(, "components": { "RigidBody": { "type": "floating" } })")),
                "scene_file::bad_body_type_is_an_error");

            // Through Lua: after save_scene and load_scene the scripts start again with their props
            WriteScript(root / "mover.lua",
                "local Mover = { props = { speed = 1 } }\n"
                "function Mover:on_start() started = (started or 0) + 1 seen = self.speed .. ' ' .. (self.target and self.target.name or '-') end\n"
                "return Mover\n");
            PhysicsSystem physics;
            if (!physics.Init()) {
                report.Check(false, "scene_file::physics_init");
                return;
            }
            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            Scene live;
            ScriptSystem system;
            if (!system.Init({ &live, &models, nullptr, nullptr, &settings, &physics }, Path(String(root.generic_string().c_str())))) {
                report.Check(false, "scene_file::script_init");
                return;
            }
            system.OnPlay();
            constexpr float Step = 1.0f / 60.0f;
            (void)SelfTestAccess::Console(system).Execute(
                "local b = elm.scene.spawn('b') b.position = elm.math.Vector3(0, 5, 0) "
                "local a = elm.scene.spawn('a') a:add('Script', { module = 'mover', props = { speed = 7, target = b } }) "
                "a:add('Collider', { shape = 'sphere', radius = 0.5 }) a:add('RigidBody', { mass = 5 })");
            system.Update(Step);
            const String file(String((root / "lua.scene").generic_string().c_str()));
            const String saveAndLoad = "elm.engine.save_scene('" + file + "') return elm.engine.load_scene('" + file + "')";
            report.Check(Evaluate(system, saveAndLoad.c_str()) == "2", "scene_file::lua_save_and_load", Evaluate(system, "return seen"));
            system.Update(Step);
            physics.Step(live, Step);
            const String restarted = Evaluate(system, "local a = elm.scene.find('a') return started, seen, a.rigidbody.mass, a.collider.shape");
            report.Check(restarted == "2\t7 b\t5.0\tsphere" && physics.GetNumBodies() == 1, "scene_file::loaded_components_come_alive", restarted);

            system.Shutdown();
            physics.Shutdown();
            fs::remove_all(root, error);
        }

        // ── Play mode ───────────────────────────────────────────────────────────

        void RunPlayModeTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_play_mode_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root, error);
            WriteScript(root / "mover.lua",
                "local Mover = { props = { step = 1 } }\n"
                "function Mover:on_start() log[#log + 1] = 'start ' .. self.entity.name end\n"
                "function Mover:on_update(dt) self.entity.position = self.entity.position + elm.math.Vector3(self.step, 0, 0) end\n"
                "function Mover:on_destroy() log[#log + 1] = 'destroy ' .. self.entity.name end\n"
                "return Mover\n");
            WriteScript(root / "main.lua",
                "local main = {}\n"
                "function main.on_start() log = {} end\n"
                "function main.on_update(dt) ticks = (ticks or 0) + 1 end\n"
                "function main.on_play() log[#log + 1] = 'play' end\n"
                "function main.on_stop() log[#log + 1] = 'stop' stopped_spawn = elm.scene.spawn('z') stopped_spawn:add('Script', 'mover') end\n"
                "return main\n");

            render::RenderResources resources;
            graphics::ModelManager models(resources);
            graphics::Model cubeModel("Cube");
            cubeModel.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            const auto cube = models.Insert(cubeModel);
            PhysicsSystem physics;
            if (!physics.Init()) {
                report.Check(false, "play::physics_init");
                return;
            }
            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            Scene scene;
            ScriptSystem system;
            PlayMode playMode(scene, &models, &physics, system);
            const EngineContext engine { &scene, &models, nullptr, nullptr, &settings, &physics, playMode.Requests() };
            if (!system.Init(engine, Path(String(root.generic_string().c_str()))) || !system.Start(settings)) {
                report.Check(false, "play::script_init");
                return;
            }
            auto eval = [&](const char* code) { return Evaluate(system, code); };
            constexpr float Step = 1.0f / 60.0f;
            // As EngineApp: physics steps in Play only, requests apply before the update
            auto frame = [&] {
                std::span<const CollisionEvent> collisions;
                if (playMode.IsPlaying()) {
                    physics.Step(scene, Step);
                    collisions = physics.CollisionEvents();
                }
                system.FixedUpdate(Step, collisions);
                playMode.ApplyRequest();
                system.Update(Step);
            };

            (void)SelfTestAccess::Console(system).Execute("a = elm.scene.spawn('a') a:add('Script', 'mover') a:add('Collider') b = elm.scene.spawn('b')");
            const NodeId nodeA = scene.IdOf(0);
            const NodeId nodeB = scene.IdOf(1);
            auto& nodeModels = scene.Components<ModelComponent>();
            nodeModels.Emplace(nodeA, ModelComponent { .model = cube });
            // An entity without a model has no Model component until a script colors or hides it
            const bool bareB = !nodeModels.Contains(nodeB);
            (void)SelfTestAccess::Console(system).Execute("b.visible = false");
            const ModelComponent* hiddenB = nodeModels.Find(nodeB);
            report.Check(bareB && hiddenB && !hiddenB->model && !hiddenB->visible && eval("return b.visible, b.model") == "false\tnil",
                "play::visible_without_a_model");
            (void)SelfTestAccess::Console(system).Execute("b.visible = true");
            for (int count = 0; count < 3; ++count)
                frame();
            report.Check(!playMode.IsPlaying() && eval("return #log, ticks, a.position.x") == "0\tnil\t0.0", "play::edit_mode_stands_still");

            const auto defaults = SelfTestAccess::Defaults(system).Get("mover");
            report.Check(defaults && defaults->size() == 1 && (*defaults)[0].first == "step" && std::get_if<int64_t>(&(*defaults)[0].second)
                    && !SelfTestAccess::Defaults(system).Get("missing.module"),
                "play::script_defaults");
            WriteScript(root / "badprops.lua", "return { props = { list = {} } }");
            WriteScript(root / "notclass.lua", "return 42");
            const auto& badProps = SelfTestAccess::Defaults(system).Get("badprops");
            const auto& notClass = SelfTestAccess::Defaults(system).Get("notclass");
            report.Check(!badProps && badProps.error().message.find("cannot be a prop: list") != String::npos && !notClass
                    && notClass.error().message.find("returns its class table") != String::npos,
                "play::script_defaults_check_the_class");
            const auto modules = SelfTestAccess::Paths(system).List();
            report.Check(std::find(modules.begin(), modules.end(), "mover") != modules.end()
                    && std::find(modules.begin(), modules.end(), "main") != modules.end(),
                "play::list_modules");

            report.Check(eval("elm.engine.play() return elm.engine.is_playing()") == "false", "play::request_waits_for_the_update");
            frame();
            report.Check(eval("return elm.engine.is_playing(), log[1], log[2], ticks, a.position.x") == "true\tplay\tstart a\t1\t1.0",
                "play::play_starts_everything");
            SelfTestAccess::Entities(system).SetField(nodeA, "step", ScriptValue(int64_t { 5 }));
            frame();
            report.Check(eval("return a.position.x") == "6.0", "play::inspector_writes_the_instance");

            // What the game does meanwhile goes away on Stop
            (void)SelfTestAccess::Console(system).Execute("b:destroy() c = elm.scene.spawn('c') a.color = elm.math.Vector4(1, 0, 0, 1) a:remove('Collider')");
            graphics::Model extraModel("Extra");
            extraModel.AddSubmesh(GeometryPrimitives::CreateCube(2.0f));
            const auto extra = models.Insert(extraModel);
            models.Remove(cube);
            frame();
            (void)SelfTestAccess::Console(system).Execute("elm.engine.stop()");
            frame();
            report.Check(eval("return log[#log - 1], log[#log], a.valid, a.position.x, b.valid, c.valid, a:has('Collider'), a.color.y")
                    == "destroy a\tstop\ttrue\t0.0\ttrue\tfalse\ttrue\t1.0",
                "play::stop_brings_the_scene_back", eval("return log[#log - 1], log[#log], a.valid, a.position.x, b.valid, c.valid"));
            // Stop replaced the stores: the one taken before Play is gone
            const auto* restoredModels = scene.FindComponents<ModelComponent>();
            const ModelComponent* restoredA = restoredModels ? restoredModels->Find(nodeA) : nullptr;
            report.Check(restoredA && models.Get(restoredA->model) && !models.Get(extra) && physics.GetNumBodies() == 0
                    && SelfTestAccess::Entities(system).All().empty(),
                "play::stop_restores_models_and_clears_runtime");
            report.Check(eval("return stopped_spawn.valid") == "false", "play::scripts_made_in_on_stop_go_too");

            const String ticksBefore = eval("return ticks");
            frame();
            report.Check(eval("return ticks, a.position.x") == ticksBefore + "\t0.0", "play::edit_mode_again");
            playMode.Play();
            frame();
            report.Check(eval("return log[#log], a.position.x") == "start a\t1.0", "play::play_again_with_the_same_entities");
            playMode.Stop();

            system.Shutdown();
            physics.Shutdown();
            fs::remove_all(root, error);
        }

        /// assets/scripts/npc/guard.lua, the NPC of demo.npcs: patrols at its post, chases a target
        /// that comes close, goes back when it is gone.
        void RunGuardTests(ScriptSystem& system, Report& report)
        {
            system.OnPlay();
            (void)SelfTestAccess::Console(system).Execute(
                "for _, e in ipairs(elm.scene.all()) do if e.valid then e:destroy() end end "
                "guard_test = { player = elm.scene.spawn('Player'), guard = elm.scene.spawn('Guard') } "
                "guard_test.player.position = elm.math.Vector3(30, 0, 0) "
                "guard_test.guard:add('Script', { module = 'npc.guard', props = { speed = 4 } })");
            for (int frame = 0; frame < 30; ++frame)
                system.Update(0.05f);
            const String patrol = Evaluate(system,
                "local g = guard_test.guard return g.script.state, g.position:distance(g.script.post) <= g.script.patrol_radius + 0.01");
            report.Check(patrol == "patrol\ttrue", "demo::guard_patrols_at_its_post", patrol);

            (void)SelfTestAccess::Console(system).Execute(
                "guard_test.player.position = guard_test.guard.position + elm.math.Vector3(3, 0, 0) "
                "guard_test.distance = guard_test.guard.position:distance(guard_test.player.position)");
            for (int frame = 0; frame < 5; ++frame)
                system.Update(0.05f);
            const String chase = Evaluate(system,
                "local g = guard_test.guard return g.script.state, g.position:distance(guard_test.player.position) < guard_test.distance");
            report.Check(chase == "chase\ttrue", "demo::guard_chases_a_close_target", chase);

            (void)SelfTestAccess::Console(system).Execute("guard_test.player:destroy()");
            for (int frame = 0; frame < 200; ++frame)
                system.Update(0.05f);
            const String back = Evaluate(system, "return guard_test.guard.script.state");
            report.Check(back == "patrol", "demo::guard_goes_back_to_its_post", back);
            (void)SelfTestAccess::Console(system).Execute("guard_test.guard:destroy() guard_test = nil");
            system.OnStop();
        }

        // ── Syntax coloring ─────────────────────────────────────────────────────

        /// "kind:text" for every span that is not plain text, to compare tokenizer output.
        String DescribeSpans(StringView source)
        {
            Vector<LuaSpan> spans;
            TokenizeLua(source, spans);
            static constexpr const char* names[] = { "text", "kw", "const", "str", "num", "comment", "builtin", "fn" };
            String described;
            uint32_t expected = 0;
            for (const auto& span : spans) {
                // The spans must cover the text in order
                if (span.begin != expected)
                    return "gap at " + String(std::to_string(span.begin));
                expected = span.end;
                if (span.kind == LuaToken::Text)
                    continue;
                described += String(names[static_cast<int>(span.kind)]) + ":" + String(source.substr(span.begin, span.end - span.begin)) + " ";
            }
            if (expected != source.size())
                return "not covered";
            return described;
        }

        void RunHighlighterTests(Report& report)
        {
            auto check = [&](const char* name, StringView source, StringView expected) {
                const String actual = DescribeSpans(source);
                report.Check(actual == expected, name, ("got: " + actual).c_str());
            };
            check("highlight::keywords_and_names", "local x = nil if not y then return true end",
                "kw:local const:nil kw:if kw:not kw:then kw:return const:true kw:end ");
            check("highlight::function_names", "function M.update(dt) end local function helper() end",
                "kw:function fn:M.update kw:end kw:local kw:function fn:helper kw:end ");
            check("highlight::builtins_not_fields", "print(elm.math.pi, t.print, self)",
                "builtin:print builtin:elm builtin:self ");
            check("highlight::strings", R"(a = "x\"y" .. 'z' .. [[long
line]] .. [==[a]]b]==])",
                "str:\"x\\\"y\" str:'z' str:[[long\nline]] str:[==[a]]b]==] ");
            check("highlight::unfinished_string_ends_at_line", "s = \"open\nx = 1",
                "str:\"open num:1 ");
            check("highlight::comments", "x = 1 -- note\n--[[ block\ncomment ]] y = 2 --[=[ ]] ]=]",
                "num:1 comment:-- note comment:--[[ block\ncomment ]] num:2 comment:--[=[ ]] ]=] ");
            check("highlight::unterminated_long_comment", "--[[ open\nstill comment",
                "comment:--[[ open\nstill comment ");
            check("highlight::numbers", "a = 3 + 0x1F + 1.5e-3 + .5 + 0x1p4 + 2E10",
                "num:3 num:0x1F num:1.5e-3 num:.5 num:0x1p4 num:2E10 ");
            check("highlight::operators_are_text", "a.b:c(d) ~= e // 2", "num:2 ");
            check("highlight::empty", "", "");
        }

        // ── Lua side ────────────────────────────────────────────────────────────

        void RunScriptTests(ScriptSystem& system, Report& report)
        {
            const Path testDirectory = SelfTestAccess::Paths(system).Root() / "tests";
            Vector<String> modules;
            std::error_code error;
            for (auto it = Directory(testDirectory).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const String file(it.CurrentPath().Filename().string().c_str());
                if (file.ends_with("_test.lua"))
                    modules.push_back("tests." + file.substr(0, file.size() - 4));
            }
            std::sort(modules.begin(), modules.end());
            report.Check(!modules.empty(), "lua::test_files_found", testDirectory.string().c_str());

            lua_State* L = SelfTestAccess::State(system);
            for (const auto& module : modules) {
                if (auto required = SelfTestAccess::Loader(system).Require(module); !required) {
                    report.Check(false, module, required.error().message);
                    continue;
                }
                if (!lua_istable(L, -1)) {
                    report.Check(false, module, "a test file must return a table of test functions");
                    lua_pop(L, 1);
                    continue;
                }

                Vector<String> tests;
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1))
                        tests.push_back(lua_tostring(L, -2));
                    lua_pop(L, 1);
                }
                std::sort(tests.begin(), tests.end());

                for (const auto& test : tests) {
                    lua_getfield(L, -1, test.c_str());
                    const auto result = SelfTestAccess::State(system).ProtectedCall(0, 0);
                    report.Check(result.has_value(), module + "::" + test, result ? "" : result.error().message);
                }
                lua_pop(L, 1);
            }
        }

    } // namespace

    int RunSelfTests(const Path& scriptRoot)
    {
        Report report;
        RunStateTests(report);
        RunNodeIdTableTests(report);
        RunSceneTests(report);
        RunSceneHierarchyTests(report);
        RunSceneOrderTests(report);
        RunSnapshotTests(report);
        RunComponentStoreTests(report);
        RunHotReloadTests(report);
        RunEntityScriptTests(report);
        RunEntityScriptListTests(report);
        RunPhysicsTests(report);
        RunPhysicsScriptTests(report);
        RunSceneFileTests(report);
        RunPlayModeTests(report);
        RunHighlighterTests(report);
        {
            // Engine systems the bindings can reach without a window; models need the renderer
            Scene scene;
            InputSystem input;
            Camera camera;
            Settings settings;
            ScriptSystem::RegisterSettings(settings);

            ScriptSystem system;
            if (auto init = system.Init({ &scene, nullptr, &input, &camera, &settings }, scriptRoot); !init) {
                std::cout << "[FAIL] init: " << init.error().message << "\n";
                return 1;
            }
            RegisterFixtures(SelfTestAccess::State(system));

            RunNativeTests(system, report);
            RunScriptTests(system, report);
            RunGuardTests(system, report);

            g_storedValue.Reset();
            lua_gc(SelfTestAccess::State(system), LUA_GCCOLLECT, 0);
            report.Check(Tracked::alive == 0, "native::userdata_collected",
                ("alive: " + std::to_string(Tracked::alive)).c_str());

            // Lua memory goes through the engine allocator: closing the state gives it all back
            const auto luaBytes = static_cast<int64_t>(SelfTestAccess::State(system).MemoryUsage());
            const int64_t before = memory::getStatistic().allocated;
            system.Shutdown();
            const int64_t released = before - memory::getStatistic().allocated;
            report.Check(released >= luaBytes, "native::lua_memory_released",
                ("lua: " + std::to_string(luaBytes) + ", released: " + std::to_string(released)).c_str());
        }
        report.Check(Tracked::alive == 0, "native::userdata_destroyed_on_close");

        std::cout << "\nScript self tests: " << report.passed << " passed, " << report.failed << " failed\n";
        return report.failed;
    }

} // namespace elm::script
