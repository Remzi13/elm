#include "script/SelfTest.hpp"

#include "script/ScriptSystem.hpp"
#include "script/ui/LuaHighlighter.hpp"
#include "script/ui/ScriptEditor.hpp"

#include "core/Memory.hpp"

#include "Scene/TestScenes.hpp"
#include "graphics/Camera.hpp"
#include "input/Input.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace elm::script {

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
            auto& state = system.GetState();
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

        // ── Scene node ids ──────────────────────────────────────────────────────

        void RunSceneTests(Report& report)
        {
            Scene scene;
            const NodeId a = scene.AddNode({ }, "a").id;
            const NodeId b = scene.AddNode({ }, "b").id;
            const NodeId c = scene.AddNode({ }, "c").id;
            report.Check(a && b && c && a != b && b != c, "scene::ids_assigned");
            report.Check(scene.IndexOf(c) == 2 && scene.Find(b)->name == "b", "scene::ids_resolve");

            // b becomes the parent of c, c sits 1 unit above b
            scene.nodes[2].parentIndex = 1;
            scene.SetLocalTransform(1, Matrix4x4::Translation(Vector3 { 5.0f, 0.0f, 0.0f }));
            scene.SetLocalTransform(2, Matrix4x4::Translation(Vector3 { 0.0f, 1.0f, 0.0f }));
            scene.UpdateWorldTransforms();
            const auto& world = scene.Find(c)->worldTransform;
            report.Check(world(0, 3) == 5.0f && world(1, 3) == 1.0f, "scene::world_follows_parent");

            report.Check(scene.RemoveNode(0), "scene::remove");
            report.Check(scene.IndexOf(a) == Scene::NoNode && !scene.Find(a), "scene::removed_id_is_stale");
            report.Check(scene.IndexOf(b) == 0 && scene.IndexOf(c) == 1 && scene.nodes[1].parentIndex == 0,
                "scene::ids_follow_moved_nodes");

            // Removing a parent keeps its child where it was in the world
            report.Check(scene.RemoveNode(scene.IndexOf(b)), "scene::remove_parent");
            const auto* child = scene.Find(c);
            scene.UpdateWorldTransforms();
            report.Check(child && child->parentIndex == Scene::NoNode && child->worldTransform(0, 3) == 5.0f
                    && child->worldTransform(1, 3) == 1.0f,
                "scene::child_keeps_world_transform");

            const NodeId d = scene.AddNode({ }, "d").id;
            report.Check(d != a && d != b && scene.IndexOf(a) == Scene::NoNode, "scene::reused_slot_new_generation");

            // Nodes edited behind the scene's back are found again
            auto copy = scene.nodes[0];
            scene.nodes.insert(scene.nodes.begin(), copy);
            report.Check(scene.IndexOf(c) == 0 || scene.IndexOf(c) == 1, "scene::lookup_survives_direct_edits");
            const NodeId copyId = scene.IdOf(1);
            report.Check(copyId && copyId != c && scene.IndexOf(c) == 0 && scene.IndexOf(copyId) == 1,
                "scene::duplicated_id_is_split");

            scene.Clear();
            report.Check(scene.IndexOf(c) == Scene::NoNode && scene.IndexOf(d) == Scene::NoNode, "scene::clear_releases_ids");
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
            auto result = system.Execute(code);
            return result ? *result : "error: " + result.error().message;
        }

        const ScriptSystem::LoadedModule* Loaded(ScriptSystem& system, StringView name)
        {
            for (const auto& module : system.LoadedModules()) {
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
            system.SetEngineContext({ nullptr, nullptr, nullptr, nullptr, &settings });
            if (!system.Init(Path(root.generic_string().c_str()))) {
                report.Check(false, "hot::init");
                return;
            }
            lua_State* L = system.GetState();

            // ── Module reload ──
            WriteScript(hot, "local M = {} M.value = 1 function M.get() return M.value end return M");
            report.Check(Evaluate(system, "hot = require 'hot' return hot.get()") == "1", "hot::module_loads");

            WriteScript(hot, "local M = {} M.value = 2 function M.get() return M.value * 10 end return M");
            Touch(hot, 2);
            const bool reloaded = system.ReloadModule("hot").has_value();
            report.Check(reloaded && Evaluate(system, "hot.get()") == "20", "hot::reload_updates_the_held_table");
            report.Check(Evaluate(system, "rawequal(hot, require 'hot')") == "true", "hot::module_keeps_its_identity");

            WriteScript(hot, "local M = {} M.value = = 3 return M");
            report.Check(!system.ReloadModule("hot") && !Loaded(system, "hot")->error.empty(), "hot::syntax_error_is_reported");
            report.Check(Evaluate(system, "hot.get()") == "20", "hot::failed_reload_keeps_the_old_version");

            WriteScript(hot, "local M = {} M.value = 3 function M.on_reload(previous) M.carried = previous.value end return M");
            report.Check(system.ReloadModule("hot").has_value() && Loaded(system, "hot")->error.empty(), "hot::fixed_module_reloads");
            report.Check(Evaluate(system, "hot.carried") == "2", "hot::on_reload_gets_the_previous_fields");
            report.Check(Evaluate(system, "hot.get") == "nil", "hot::removed_fields_disappear");

            WriteScript(hot, "local M = { counter = 0 } function M.inc() M.counter = M.counter + 1 end return M");
            (void)system.ReloadModule("hot");
            report.Check(Evaluate(system, "hot.inc() hot.inc() return hot.counter") == "2",
                "hot::module_code_sees_the_held_table");

            WriteScript(hot, "local state = elm.script.persist('hot', { count = 0 }) state.count = state.count + 1 return { count = function() return state.count end }");
            (void)system.ReloadModule("hot");
            (void)system.ReloadModule("hot");
            report.Check(Evaluate(system, "hot.count()") == "2", "hot::persist_survives_reloads");

            WriteScript(hot, "local M = {} M.value = 4 return M");
            Touch(hot, 4);
            report.Check(system.ReloadChanged() == 1 && Evaluate(system, "hot.value") == "4", "hot::reload_changed_finds_the_file");
            report.Check(system.ReloadChanged() == 0, "hot::reload_changed_skips_unchanged");

            // Polling: the change is applied once two polls see the same file time
            WriteScript(hot, "local M = {} M.value = 5 return M");
            Touch(hot, 6);
            for (int poll = 0; poll < 3; ++poll) {
                system.PollFileChanges();
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
            }
            report.Check(Evaluate(system, "hot.value") == "5", "hot::poll_reloads_changed_files");

            // ── Main module ──
            WriteScript(mainFile, "local main = {} function main.on_start() started = (started or 0) + 1 end return main =");
            report.Check(!system.Start(settings) && system.GetMainState() == ScriptSystem::MainState::Idle
                    && Loaded(system, "main") && !Loaded(system, "main")->error.empty(),
                "hot::broken_main_is_idle");
            WriteScript(mainFile,
                "local main = {}\n"
                "function main.on_start() started = (started or 0) + 1 end\n"
                "function main.on_update(dt) if fail then error('update failed') end ticks = (ticks or 0) + 1 end\n"
                "return main\n");
            report.Check(system.ReloadModule("main").has_value() && system.GetMainState() == ScriptSystem::MainState::Running
                    && Evaluate(system, "started") == "1",
                "hot::fixed_main_starts");

            (void)system.Execute("fail = true");
            system.Update(0.016f);
            report.Check(system.GetMainState() == ScriptSystem::MainState::Faulted
                    && system.MainError().find("update failed") != String::npos,
                "hot::failing_hook_faults_the_main_module");
            (void)system.Execute("fail = false");
            report.Check(system.ReloadModule("main").has_value() && system.GetMainState() == ScriptSystem::MainState::Running,
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
            const auto modules = system.Complete("elm.sc");
            report.Check(has(modules, "elm.scene") && has(modules, "elm.script"), "console::complete_module");
            report.Check(has(system.Complete("x = elm.math.Vector3.u"), "x = elm.math.Vector3.up"), "console::complete_in_expression");
            (void)system.Execute("vv = elm.math.Vector3(1, 2, 3)");
            report.Check(has(system.Complete("vv:norm"), "vv:normalized") && has(system.Complete("vv.le"), "vv.length")
                    && !has(system.Complete("vv:le"), "vv:length"),
                "console::complete_userdata_members");
            report.Check(system.Complete("nothing.here.").empty(), "console::complete_unknown_is_empty");
            report.Check(lua_gettop(L) == 0, "console::stack_balanced");

            // ── Module names and main functions ──
            const Path scriptRoot(root.generic_string().c_str());
            report.Check(system.ModuleNameOf(scriptRoot / "hot.lua") == "hot"
                    && system.ModuleNameOf(scriptRoot / "a/b.lua") == "a.b"
                    && system.ModuleNameOf(scriptRoot / "pkg/init.lua") == "pkg"
                    && system.ModuleNameOf(scriptRoot / "notes.txt").empty()
                    && system.ModuleNameOf(scriptRoot / "../outside.lua").empty(),
                "hot::module_name_of_file");
            WriteScript(mainFile,
                "local main = {}\n"
                "function main.build_scene() built = (built or 0) + 1 end\n"
                "function main.broken() error('build failed') end\n"
                "return main\n");
            (void)system.ReloadModule("main");
            const auto built = system.CallMain("build_scene");
            const auto missing = system.CallMain("missing");
            const auto broken = system.CallMain("broken");
            report.Check(built && *built && Evaluate(system, "built") == "1" && missing && !*missing
                    && !broken && broken.error().message.find("build failed") != String::npos
                    && system.GetMainState() == ScriptSystem::MainState::Running,
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
            system.RequestRestart();
            system.Update(0.016f);
            report.Check(system.IsInitialized() && system.GetMainState() == ScriptSystem::MainState::Running
                    && Evaluate(system, "built") == "nil" && Evaluate(system, "hot") == "nil",
                "hot::restart_starts_a_fresh_state");

            system.Shutdown();
            fs::remove_all(root, error);
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
            const Path testDirectory = system.ScriptRoot() / "tests";
            Vector<String> modules;
            std::error_code error;
            for (auto it = Directory(testDirectory).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
                const String file(it.CurrentPath().Filename().string().c_str());
                if (file.ends_with("_test.lua"))
                    modules.push_back("tests." + file.substr(0, file.size() - 4));
            }
            std::sort(modules.begin(), modules.end());
            report.Check(!modules.empty(), "lua::test_files_found", testDirectory.string().c_str());

            lua_State* L = system.GetState();
            for (const auto& module : modules) {
                if (auto required = system.Require(module); !required) {
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
                    const auto result = system.GetState().ProtectedCall(0, 0);
                    report.Check(result.has_value(), module + "::" + test, result ? "" : result.error().message);
                }
                lua_pop(L, 1);
            }
        }

    } // namespace

    int RunSelfTests(const Path& scriptRoot)
    {
        Report report;
        RunSceneTests(report);
        RunHotReloadTests(report);
        RunHighlighterTests(report);
        {
            // Engine systems the bindings can reach without a window; models need the renderer
            Scene scene;
            InputSystem input;
            Camera camera;
            Settings settings;
            ScriptSystem::RegisterSettings(settings);

            ScriptSystem system;
            system.SetEngineContext({ &scene, nullptr, &input, &camera, &settings });
            if (auto init = system.Init(scriptRoot); !init) {
                std::cout << "[FAIL] init: " << init.error().message << "\n";
                return 1;
            }
            RegisterFixtures(system.GetState());

            RunNativeTests(system, report);
            RunScriptTests(system, report);

            g_storedValue.Reset();
            lua_gc(system.GetState(), LUA_GCCOLLECT, 0);
            report.Check(Tracked::alive == 0, "native::userdata_collected",
                ("alive: " + std::to_string(Tracked::alive)).c_str());

            // Lua memory goes through the engine allocator: closing the state gives it all back
            const auto luaBytes = static_cast<int64_t>(system.GetState().MemoryUsage());
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
