#include "editor/EditorSelfTest.hpp"

#include "editor/EditMode.hpp"
#include "editor/NodeEditing.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/SceneEdits.hpp"
#include "editor/script/CodeIndent.hpp"
#include "editor/script/ErrorLocation.hpp"
#include "editor/script/LuaHighlighter.hpp"
#include "graphics/gizmo/GizmoSettings.hpp"
#include "editor/ui/EditorToolbar.hpp"
#include "editor/ui/NodeInspectorWindow.hpp"
#include "editor/ui/SceneHierarchyWindow.hpp"
#include "editor/ui/SceneMenu.hpp"

#include "model/ModelCatalog.hpp"

#include "render/api/FrameWriter.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneSystem.hpp"
#include "Scene/ScriptComponent.hpp"

#include "physics/PhysicsSystem.hpp"

#include "resource/ResourceSystem.hpp"
#include "resource/Serializer.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

#include "math/Primitivs.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace elm::editor {

    namespace {

        using namespace script;
        using namespace math;

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

        // ── Editor indentation ──────────────────────────────────────────────────

        /// The edited text with the selection marked: "[" at its begin, "]" at its end (the cursor).
        String DescribeIndent(StringView text, size_t begin, size_t end, IndentEdit edit)
        {
            const IndentedText edited = ApplyIndentEdit(text, begin, end, edit);
            String described = edited.text;
            described.insert(edited.selectionEnd, "]");
            described.insert(edited.selectionBegin, "[");
            return described;
        }

        void RunCodeIndentTests(Report& report)
        {
            auto check = [&](const char* name, const String& actual, StringView expected) {
                report.Check(actual == expected, name, ("got: " + actual).c_str());
            };
            check("indent::tab_at_cursor", DescribeIndent("ab", 1, 1, IndentEdit::Indent), "a    []b");
            check("indent::tab_replaces_selection_in_a_line", DescribeIndent("abc", 1, 2, IndentEdit::Indent), "a    []c");
            check("indent::selected_lines_skip_empty", DescribeIndent("a\n\nb\nc", 0, 4, IndentEdit::Indent), "[    a\n\n    b]\nc");
            check("indent::selection_after_newline_leaves_next_line", DescribeIndent("a\nb\n", 0, 2, IndentEdit::Indent), "[    a\n]b\n");
            check("indent::outdent_one_level_or_tab", DescribeIndent("      a\n\tb\nc", 0, 12, IndentEdit::Outdent), "[  a\nb\nc]");
            check("indent::outdent_cursor_in_indentation", DescribeIndent("    x", 2, 2, IndentEdit::Outdent), "[]x");
            check("indent::line_indent", String(LineIndent("  a\n\t b", 6)), "\t ");
        }

        // ── Error locations ─────────────────────────────────────────────────────

        void RunErrorLocationTests(Report& report)
        {
            const auto syntax = FindErrorLocation("demo/spinner.lua:12: unexpected symbol near '='");
            const auto loading = FindErrorLocation("error loading module 'main':\n\tmain.lua:3: <eof> expected");
            const auto runtime = FindErrorLocation("on_update: [string \"x\"]:1: oops\nstack traceback:\n\tgame/player.lua:40: in function");
            report.Check(syntax && syntax->file == "demo/spinner.lua" && syntax->line == 12
                    && loading && loading->file == "main.lua" && loading->line == 3
                    && runtime && runtime->file == "game/player.lua" && runtime->line == 40
                    && !FindErrorLocation("no location here.lua: at all"),
                "editor::error_location");
        }

        // ── Edit mode ───────────────────────────────────────────────────────────

        /// Where node `a` is along x: the mover script walks it there in Play.
        float PositionX(const Scene& scene, NodeId node)
        {
            const Scene::Node* found = scene.Find(node);
            return found ? found->localTransform(0, 3) : -1.0f;
        }

        /// The editor's Edit mode around the engine's PlayMode: the edited scene comes back after Stop.
        void RunEditModeTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_edit_mode_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root, error);
            std::ofstream(root / "mover.lua") << "local Mover = {}\n"
                "function Mover:on_update(dt) self.entity.position = self.entity.position + elm.math.Vector3(1, 0, 0) end\n"
                "return Mover\n";

            resource::ResourceSystem resources;
            ModelCatalog& models = resources.GetModels();
            Model cubeModel("Cube");
            cubeModel.AddSubmesh(GeometryPrimitives::CreateCube(1.0f));
            const auto cube = models.Insert(cubeModel);
            PhysicsSystem physics;
            if (!physics.Init()) {
                report.Check(false, "edit::physics_init");
                return;
            }
            Settings settings;
            ScriptSystem::RegisterSettings(settings);
            SceneSystem sceneSystem(models);
            Scene& scene = sceneSystem.EditScene();
            ScriptSystem scripts({ .scene = &sceneSystem, .physics = &physics, .resources = &resources, .settings = &settings });
            PlayMode& play = scripts.GetPlayMode();
            bool hasStartScene = false;
            play.SetStartSource([&](PlayTarget target) -> std::optional<PlayStart> {
                PlayStart start;
                if (target == PlayTarget::Project) {
                    if (!hasStartScene)
                        return std::nullopt;
                    SceneAsset::Node level;
                    level.name = "level";
                    start.scene.emplace();
                    start.scene->nodes.push_back(std::move(level));
                }
                return start;
            });
            if (!scripts.Init(Path(String(root.generic_string().c_str())))) {
                report.Check(false, "edit::script_init");
                return;
            }
            EditMode editMode(sceneSystem, scripts);

            const NodeId a = scene.AddNode("a");
            const NodeId b = scene.AddNode("b");
            ScriptComponent mover;
            mover.module = "mover";
            scene.Components<ScriptComponent>().Emplace(a, std::move(mover));
            scene.Components<Collider>().Emplace(a);
            scene.Components<ModelComponent>().Emplace(a, ModelComponent { .model = cube });
            constexpr float Step = 1.0f / 60.0f;
            // As Editor::Run: requests before the simulation, physics steps in Play only
            auto frame = [&] {
                editMode.ApplyRequest();
                if (play.IsPlaying())
                    physics.Step(scene, Step);
                scripts.FixedUpdate(Step);
                scripts.Update(Step);
                scene.UpdateWorldTransforms();
            };

            play.RequestPlay(PlayTarget::Scene);
            frame();
            frame();
            report.Check(play.IsPlaying() && PositionX(scene, a) > 0.0f, "edit::play_runs_the_edited_scene");
            (void)scene.RemoveNode(b);
            (void)scene.AddNode("spawned");
            models.Remove(cube);

            play.RequestStop();
            frame();
            const ModelComponent* model = scene.Components<ModelComponent>().Find(a);
            report.Check(!play.IsPlaying() && scene.NodeCount() == 2 && scene.Find(a) && scene.Find(b) && PositionX(scene, a) == 0.0f
                    && scene.Components<Collider>().Contains(a) && model && models.Get(model->model) && physics.GetNumBodies() == 0,
                "edit::stop_brings_the_edited_scene_back");

            play.RequestPlay(PlayTarget::Project);
            frame();
            report.Check(!play.IsPlaying() && scene.NodeCount() == 2 && PositionX(scene, a) == 0.0f,
                "edit::play_without_a_start_leaves_the_scene");

            hasStartScene = true;
            play.RequestPlay(PlayTarget::Project);
            frame();
            report.Check(play.IsPlaying() && scene.NodeCount() == 1 && scene.Nodes()[0].name == "level", "edit::project_plays_the_start_scene");
            editMode.Stop();
            report.Check(!play.IsPlaying() && scene.NodeCount() == 2 && scene.IndexOf(a) == 0 && scene.Find(a)->name == "a",
                "edit::stop_now_brings_the_edited_scene_back");

            scripts.Shutdown();
            physics.Shutdown();
            fs::remove_all(root, error);
        }

        /// The history of the user's edits: node steps found around a UI frame, gestures, structure steps.
        void RunSceneEditsTests(Report& report)
        {
            Scene scene;
            const NodeId a = scene.AddNode("a");
            SceneEdits edits(scene);
            // One UI frame that moved the node, as a window would
            auto frame = [&](NodeId watched, bool held, const auto& edit) {
                edits.BeginFrame(watched);
                edit();
                edits.EndFrame(held);
            };
            auto x = [&](NodeId node) { return scene.Find(node)->localTransform(0, 3); };

            frame(a, false, [&] { (void)scene.SetLocalTransform(a, Matrix4x4::Translation({ 1.0f, 0.0f, 0.0f })); });
            report.Check(edits.CanUndo() && edits.Undo() && x(a) == 0.0f && edits.CanRedo(), "edits::undo_a_move");
            report.Check(edits.Redo() && x(a) == 1.0f, "edits::redo_a_move");

            // A drag: every frame of it moves the node while the widget is held, it is one step
            for (int step = 2; step <= 4; ++step)
                frame(a, true, [&] { (void)scene.SetLocalTransform(a, Matrix4x4::Translation({ static_cast<float>(step), 0.0f, 0.0f })); });
            frame(a, false, [] { });
            report.Check(edits.Undo() && x(a) == 1.0f, "edits::a_gesture_is_one_step");
            (void)edits.Redo();
            // Released between two edits: two steps
            frame(a, false, [&] { (void)scene.SetName(a, "renamed"); });
            frame(a, false, [&] { scene.Components<RigidBody>().Emplace(a); });
            report.Check(edits.Undo() && !scene.Components<RigidBody>().Contains(a) && scene.Find(a)->name == "renamed",
                "edits::undo_takes_the_component_away");
            report.Check(edits.Undo() && scene.Find(a)->name == "a" && x(a) == 4.0f, "edits::separate_edits_are_separate_steps");
            frame(a, false, [&] { (void)scene.SetName(a, "other"); });
            report.Check(!edits.CanRedo(), "edits::a_new_edit_drops_redo");

            // Structure: undo brings a removed node back with its id
            const NodeId b = scene.AddNode("b");
            frame(a, false, [] { });
            frame(b, false, [&] { scene.Components<Collider>().Emplace(b); });
            frame(b, false, [&] { (void)scene.RemoveNode(b); });
            report.Check(edits.Undo() && scene.Find(b) && scene.Components<Collider>().Contains(b), "edits::undo_a_removal_keeps_the_node_id");
            report.Check(edits.Redo() && !scene.Find(b), "edits::redo_a_removal");
            report.Check(edits.Undo() && edits.Undo() && scene.Find(b) && !scene.Components<Collider>().Contains(b),
                "edits::node_steps_fit_a_restored_node");

            // Play freezes the history; Stop gives the scene back and the steps go on
            edits.SetFrozen(true);
            frame(a, false, [&] { (void)scene.SetName(a, "played"); });
            report.Check(!edits.CanUndo() && !edits.Undo(), "edits::frozen_in_play");
            (void)scene.SetName(a, "other");
            edits.SetFrozen(false);
            report.Check(edits.CanUndo(), "edits::steps_after_play");

            edits.Clear();
            report.Check(!edits.CanUndo() && !edits.CanRedo(), "edits::clear");
            for (size_t step = 0; step < SceneEdits::MaxSteps + 5; ++step)
                frame(a, false, [&] { (void)scene.SetName(a, "n" + String(std::to_string(step))); });
            size_t undone = 0;
            while (edits.Undo())
                ++undone;
            report.Check(undone == SceneEdits::MaxSteps, "edits::the_oldest_steps_go");
        }

        // ── Saved scene and node edits, without UI ──────────────────────────────

        void RunSavedMarkTests(Report& report)
        {
            Scene scene;
            const NodeId a = scene.AddNode("a");
            SceneEdits edits(scene);
            auto rename = [&](const char* name) {
                edits.BeginFrame(a);
                (void)scene.SetName(a, name);
                edits.EndFrame(false);
            };
            report.Check(!edits.IsModified(), "saved::a_fresh_scene_is_not_modified");
            rename("b");
            report.Check(edits.IsModified(), "saved::an_edit_modifies");
            edits.MarkSaved();
            report.Check(!edits.IsModified(), "saved::save_clears_it");
            (void)edits.Undo();
            report.Check(edits.IsModified(), "saved::undo_past_the_save_modifies");
            (void)edits.Redo();
            report.Check(!edits.IsModified(), "saved::redo_back_to_the_save");
            (void)edits.Undo();
            rename("c");
            (void)edits.Undo();
            report.Check(edits.IsModified(), "saved::a_lost_save_stays_modified");
            edits.Clear();
            report.Check(!edits.IsModified(), "saved::another_scene_is_not_modified");
        }

        void RunNodeEditingTests(Report& report)
        {
            Model cube = MakePrimitiveModel(ModelPrimitive::Cube);
            const Collider fitted = FitColliderToModel(&cube);
            report.Check(fitted.halfExtents.x == 0.5f && fitted.radius == 0.5f, "nodes::collider_fits_the_model");
            Model plane = MakePrimitiveModel(ModelPrimitive::GroundPlane);
            report.Check(FitColliderToModel(&plane).halfExtents.y > 0.0f, "nodes::collider_of_a_flat_model_has_thickness");

            ScriptComponent script;
            SetOverride(script, "speed", ScriptValue { 2.0 });
            SetOverride(script, "armor", ScriptValue { 1.0 });
            SetOverride(script, "speed", ScriptValue { 3.0 });
            const ScriptValue* speed = FindOverride(script, "speed");
            report.Check(script.props.size() == 2 && script.props[0].first == "armor" && speed && std::get<double>(*speed) == 3.0,
                "nodes::overrides_stay_sorted_and_single");
            RemoveOverride(script, "speed");
            report.Check(!FindOverride(script, "speed"), "nodes::override_removed");

            Scene scene;
            const NodeId a = scene.AddNode("a");
            scene.Components<Collider>().Emplace(a);
            const NodeId copy = DuplicateNode(scene, a);
            report.Check(scene.Find(copy) && scene.Find(copy)->name == "a Copy" && scene.Components<Collider>().Contains(copy),
                "nodes::a_copy_has_the_components");
            report.Check(RemoveNodeFromPlace(scene, a) == copy, "nodes::removal_gives_the_next_node");
            report.Check(!RemoveNodeFromPlace(scene, copy) && scene.Empty(), "nodes::removing_the_last_node_gives_nothing");

            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_module_list_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root / "game" / "npc", error);
            fs::create_directories(root / ".meta", error);
            std::ofstream(root / "main.lua") << "";
            std::ofstream(root / "init.lua") << "";
            std::ofstream(root / "game" / "init.lua") << "";
            std::ofstream(root / "game" / "npc" / "guard.lua") << "";
            std::ofstream(root / ".meta" / "elm.lua") << "";
            const Vector<String> modules = ListScriptModules(Path(String(root.generic_string().c_str())));
            report.Check(modules == Vector<String> { "game", "game.npc.guard", "main" }, "nodes::script_modules_by_require_names");
            fs::remove_all(root, error);
        }

        /// The open scene as a file; in Play the scene is the running game, Stop brings the edited one back: replacing
        /// it would leave the document on a file of another scene, and the next Save would overwrite that file.
        void RunSceneDocumentTests(Report& report, const Path& assets)
        {
            const Path scenes = assets / "scenes";
            const Path otherFile = scenes / "other.scene";
            {
                Scene other;
                (void)other.AddNode("other");
                const ModelCatalog noModels;
                report.Check(resource::Serializer::SaveScene(other, noModels, assets, otherFile).has_value(), "document::other_scene_written");
            }
            const auto otherSceneIsIntact = [&] {
                const auto loaded = resource::Serializer::LoadScene(otherFile, assets);
                return loaded && loaded->nodes.size() == 1 && loaded->nodes[0].name == "other";
            };

            Scene scene;
            ModelCatalog models;
            SceneEdits edits(scene);
            Selection selection;
            bool playing = false;
            script::PlayRequests play;
            play.isPlaying = [&] { return playing; };
            SceneDocument document(SceneDocumentDeps { scene, models, edits, selection, play }, assets, scenes, scenes / "opened.scene");
            report.Check(document.ShownName() == "opened.scene", "document::shows_the_name_in_the_scenes_folder");

            const NodeId node = scene.AddNode("node");
            edits.BeginFrame(node);
            edits.EndFrame(false);
            report.Check(document.IsModified(), "document::a_new_node_modifies");

            // What EditMode keeps for Stop
            Scene edited = scene.Clone();
            playing = true;
            edits.SetFrozen(true);
            document.Open(otherFile);
            report.Check(scene.NodeCount() == 1 && scene.Find(node) && document.File() == scenes / "opened.scene"
                    && document.Status().kind == SceneStatus::Kind::Failure,
                "document::no_scene_file_opens_in_play");
            document.New();
            report.Check(scene.Find(node) && edits.IsModified(), "document::no_new_scene_in_play");
            report.Check(!document.Save(), "document::no_save_in_play");

            scene = std::move(edited);
            playing = false;
            edits.SetFrozen(false);
            report.Check(edits.CanUndo(), "document::play_keeps_the_history");
            report.Check(document.Save() && !document.IsModified() && otherSceneIsIntact(), "document::save_after_play_leaves_other_scene_files");

            document.Open(otherFile);
            report.Check(scene.NodeCount() == 1 && scene.Nodes()[0].name == "other" && document.File() == otherFile && !document.IsModified(),
                "document::a_scene_file_opens_in_edit");
            document.New();
            report.Check(scene.Empty() && document.ShownName().starts_with("new") && !edits.CanUndo(), "document::new_takes_an_unused_name");
            document.Open(scenes / "no_such.scene");
            report.Check(document.Status().kind == SceneStatus::Kind::Failure && document.ShownName().starts_with("new"),
                "document::an_unreadable_file_changes_nothing");
        }

        // ── Scene windows ───────────────────────────────────────────────────────

        /// ImGui without a window system: frames run and take the test's mouse, nothing is drawn.
        class HeadlessImGui {
        public:
            HeadlessImGui()
            {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = ImVec2(1600.0f, 1000.0f);
                io.DeltaTime = 1.0f / 60.0f;
                unsigned char* pixels = nullptr;
                int width = 0;
                int height = 0;
                io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
            }
            ~HeadlessImGui() { ImGui::DestroyContext(); }

            HeadlessImGui(const HeadlessImGui&) = delete;
            HeadlessImGui& operator=(const HeadlessImGui&) = delete;
        };

        /// Scene Hierarchy, Inspector and the Scene menu on a scene of their own, in a fresh ImGui.
        struct WindowsFixture {
            static constexpr float ScanStep = 8.0f;

            explicit WindowsFixture(const Path& assets)
                : document(SceneDocumentDeps { scene, models, edits, selection, play }, assets, assets / "scenes", assets / "scenes" / "opened.scene")
                , hierarchy(settings, SceneHierarchyDeps { scene, models, edits, document, selection, sceneFile }, assets, assets / "models")
                , inspector(settings, NodeInspectorDeps { scene, models, edits, document, selection, gizmo, play, scripts, files, sceneFile },
                      NodeInspectorWindow::Folders { assets, assets / "models", assets / "scripts" })
            {
                play.isPlaying = [this] { return playing; };
                menu.SetDocument(&document, &edits);
                // Side by side: new windows open on top of each other
                Frame();
                ImGui::SetWindowPos(SceneHierarchyWindow::Name, ImVec2(0.0f, 0.0f));
                ImGui::SetWindowPos(NodeInspectorWindow::Name, ImVec2(330.0f, 0.0f));
                ImGui::SetWindowSize(NodeInspectorWindow::Name, ImVec2(500.0f, 900.0f));
                Frame();
            }

            void Frame()
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(1500.0f, 950.0f));
                ImGui::Begin("Menus", nullptr, ImGuiWindowFlags_NoDecoration);
                menu.DrawPopups();
                ImGui::End();
                hierarchy.Render(writer);
                inspector.Render(writer);
                ImGui::EndFrame();
            }

            void MoveTo(ImVec2 position)
            {
                ImGui::GetIO().AddMousePosEvent(position.x, position.y);
                Frame();
                Frame();
            }

            void SetButton(ImGuiMouseButton button, bool down)
            {
                ImGui::GetIO().AddMouseButtonEvent(button, down);
                Frame();
            }

            void Click(ImVec2 position, ImGuiMouseButton button = ImGuiMouseButton_Left)
            {
                MoveTo(position);
                SetButton(button, true);
                SetButton(button, false);
                Frame();
            }

            void Drag(ImVec2 from, ImVec2 to)
            {
                MoveTo(from);
                SetButton(ImGuiMouseButton_Left, true);
                MoveTo(ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f));
                MoveTo(to);
                SetButton(ImGuiMouseButton_Left, false);
                Frame();
            }

            void Type(const char* text)
            {
                ImGui::GetIO().AddInputCharactersUTF8(text);
                Frame();
                Frame();
            }

            void Press(ImGuiKey key)
            {
                ImGui::GetIO().AddKeyEvent(key, true);
                Frame();
                ImGui::GetIO().AddKeyEvent(key, false);
                Frame();
            }

            /// A window by a part of its name: child windows are named after their parents.
            [[nodiscard]] static ImGuiWindow* FindWindow(StringView part)
            {
                for (ImGuiWindow* found : GImGui->Windows) {
                    if (StringView(found->Name).find(part) != StringView::npos)
                        return found;
                }
                return nullptr;
            }

            /// The menu opened last.
            [[nodiscard]] static ImGuiWindow* TopPopup()
            {
                return GImGui->OpenPopupStack.empty() ? nullptr : GImGui->OpenPopupStack.back().Window;
            }

            /// A modal dialog is open: a file dialog or the question about unsaved changes.
            [[nodiscard]] static bool IsDialogOpen() { return ImGui::GetTopMostPopupModal() != nullptr; }

            [[nodiscard]] static ImVec2 RowCenter(size_t row)
            {
                const ImGuiWindow* tree = FindWindow("TreeScrollArea");
                const float height = ImGui::GetFontSize() + GImGui->Style.CellPadding.y * 2.0f;
                return ImVec2(tree->InnerRect.GetCenter().x, tree->InnerRect.Min.y + (static_cast<float>(row) + 0.5f) * height);
            }

            [[nodiscard]] static ImVec2 RowArrow(size_t row)
            {
                const ImGuiWindow* tree = FindWindow("TreeScrollArea");
                return ImVec2(tree->InnerRect.Min.x + GImGui->Style.CellPadding.x + ImGui::GetFontSize() * 0.5f, RowCenter(row).y);
            }

            /// Finds a widget by its ImGui id, moving the mouse over the window, and clicks it.
            bool ClickItem(ImGuiWindow* target, ImGuiID id)
            {
                if (!target)
                    return false;
                const ImRect area = target->InnerRect;
                for (float y = area.Min.y + 1.0f; y < area.Max.y; y += ScanStep) {
                    for (float x = area.Min.x + 1.0f; x < area.Max.x; x += ScanStep) {
                        ImGui::GetIO().AddMousePosEvent(x, y);
                        Frame();
                        if (GImGui->HoveredId == id) {
                            Click(ImVec2(x, y));
                            Frame();
                            return true;
                        }
                    }
                }
                return false;
            }

            bool ClickItem(ImGuiWindow* target, const char* label)
            {
                return target && ClickItem(target, target->GetID(label));
            }

            /// A widget of a grid in a section of Inspector.
            bool ClickInSection(const char* section, const char* label)
            {
                ImGuiWindow* window = FindWindow(NodeInspectorWindow::Name);
                if (!window)
                    return false;
                const ImGuiID grid = ImHashStr("Grid", 0, window->GetID(section));
                return ClickItem(window, ImHashStr(label, 0, grid));
            }

            /// The item of the menu opened last. Down the middle of the menu: a submenu covers the edge of its
            /// parent, the mouse there would close it.
            bool ClickMenuItem(const char* label)
            {
                ImGuiWindow* popup = TopPopup();
                if (!popup)
                    return false;
                const ImGuiID id = popup->GetID(label);
                const ImRect area = popup->InnerRect;
                for (float y = area.Min.y + 1.0f; y < area.Max.y; y += ScanStep) {
                    ImGui::GetIO().AddMousePosEvent(area.GetCenter().x, y);
                    Frame();
                    if (GImGui->HoveredId == id) {
                        Click(ImVec2(area.GetCenter().x, y));
                        Frame();
                        return true;
                    }
                }
                return false;
            }

            HeadlessImGui imgui;
            Settings settings;
            Scene scene;
            ModelCatalog models;
            SceneEdits edits { scene };
            Selection selection;
            GizmoSettings gizmo { GizmoOperation::Translate, GizmoSpace::World };
            bool playing { false };
            script::PlayRequests play;
            ScriptInspection scripts;
            FileOpener files;
            SceneFileRequests sceneFile;
            render::FrameWriter writer;
            SceneDocument document;
            SceneMenu menu;
            SceneHierarchyWindow hierarchy;
            NodeInspectorWindow inspector;
        };

        void RunTreeDropTests(Report& report, const Path& assets)
        {
            {
                WindowsFixture fixture(assets);
                const NodeId parent = fixture.scene.AddNode("parent");
                const NodeId child = fixture.scene.AddNode("child", Matrix4x4::Identity(), parent);
                fixture.Frame();
                fixture.Click(WindowsFixture::RowArrow(0));
                fixture.Click(WindowsFixture::RowCenter(1));
                report.Check(fixture.selection.node == child, "tree::an_open_row_shows_its_child");
                fixture.Drag(WindowsFixture::RowCenter(1), WindowsFixture::RowCenter(6));
                report.Check(!fixture.scene.Find(child)->parent, "tree::drop_on_empty_space_makes_a_root");
            }
            {
                WindowsFixture fixture(assets);
                const NodeId first = fixture.scene.AddNode("first");
                const NodeId last = fixture.scene.AddNode("last");
                fixture.Frame();
                fixture.Drag(WindowsFixture::RowCenter(0), WindowsFixture::RowCenter(1));
                report.Check(fixture.scene.Find(first)->parent == last, "tree::drop_on_the_last_row_makes_it_the_parent");
            }
        }

        void RunTreeSearchTests(Report& report, const Path& assets)
        {
            WindowsFixture fixture(assets);
            const NodeId parent = fixture.scene.AddNode("parent");
            const NodeId needle = fixture.scene.AddNode("needle", Matrix4x4::Identity(), parent);
            fixture.Frame();
            // The user opens the parent and closes it again
            fixture.Click(WindowsFixture::RowArrow(0));
            fixture.Click(WindowsFixture::RowArrow(0));

            const ImGuiWindow* tree = WindowsFixture::FindWindow("TreeScrollArea");
            const ImVec2 treeStart = tree->ParentWindow->DC.CursorStartPos;
            // Past the end of the text typed: a click there leaves the cursor after it, for the backspaces
            const ImVec2 filter(treeStart.x + 150.0f, treeStart.y + ImGui::GetFrameHeight() * 0.5f);
            fixture.Click(filter);
            fixture.Type("needle");
            fixture.Click(WindowsFixture::RowCenter(1));
            report.Check(fixture.selection.node == needle, "tree::search_shows_a_match_in_a_closed_parent");

            fixture.Click(filter);
            for (size_t letter = 0; letter < std::strlen("needle"); ++letter)
                fixture.Press(ImGuiKey_Backspace);
            fixture.selection.node = parent;
            fixture.Click(WindowsFixture::RowCenter(1));
            report.Check(fixture.selection.node == NodeId { }, "tree::search_leaves_a_closed_parent_closed");
        }

        void RunTreeEditTests(Report& report, const Path& assets)
        {
            WindowsFixture fixture(assets);
            const NodeId node = fixture.scene.AddNode("node");
            fixture.Frame();
            report.Check(fixture.selection.node == NodeId { }, "tree::nothing_is_selected_at_first");
            fixture.Click(WindowsFixture::RowCenter(0));
            report.Check(fixture.selection.node == node, "tree::a_click_selects");

            fixture.Press(ImGuiKey_F2);
            fixture.Frame();
            fixture.Type("x");
            fixture.Press(ImGuiKey_Enter);
            report.Check(fixture.scene.Find(node)->name == "x", "tree::f2_renames");
            fixture.Press(ImGuiKey_F2);
            fixture.Frame();
            fixture.Type("y");
            fixture.Press(ImGuiKey_Escape);
            report.Check(fixture.scene.Find(node)->name == "x", "tree::escape_keeps_the_name");

            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
            fixture.Press(ImGuiKey_D);
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
            fixture.Frame();
            const NodeId copy = fixture.selection.node;
            report.Check(fixture.scene.NodeCount() == 2 && copy != node, "tree::ctrl_d_duplicates");

            fixture.Press(ImGuiKey_Delete);
            report.Check(fixture.scene.NodeCount() == 1 && !fixture.scene.Find(copy), "tree::delete_removes");

            fixture.Click(WindowsFixture::RowCenter(0), ImGuiMouseButton_Right);
            fixture.Frame();
            const bool duplicated = fixture.ClickMenuItem("Duplicate");
            report.Check(duplicated && fixture.scene.NodeCount() == 2, "tree::row_menu_duplicates_the_node");
            fixture.Click(WindowsFixture::RowCenter(0), ImGuiMouseButton_Right);
            fixture.Frame();
            const bool deleted = fixture.ClickMenuItem("Delete Node");
            report.Check(deleted && fixture.scene.NodeCount() == 1 && !fixture.scene.Find(node), "tree::row_menu_deletes_the_node");

            fixture.Click(WindowsFixture::RowCenter(6));
            report.Check(fixture.selection.node == NodeId { }, "tree::a_click_on_empty_space_selects_nothing");

            fixture.Click(WindowsFixture::RowCenter(6), ImGuiMouseButton_Right);
            fixture.Frame();
            const bool chosen = fixture.ClickMenuItem("Model from Disk...");
            report.Check(chosen && WindowsFixture::IsDialogOpen(), "tree::tree_menu_opens_the_model_dialog");
        }

        void RunInspectorTests(Report& report, const Path& assets)
        {
            {
                WindowsFixture fixture(assets);
                const NodeId node = fixture.scene.AddNode("node");
                fixture.selection.node = node;
                fixture.Frame();
                fixture.Frame();
                const bool opened = fixture.ClickItem(WindowsFixture::FindWindow(NodeInspectorWindow::Name), "+ Add Component");
                const bool menuOpened = opened && fixture.ClickMenuItem("Model");
                const bool chosen = menuOpened && fixture.ClickMenuItem("Browse from Disk...");
                report.Check(chosen && WindowsFixture::IsDialogOpen(), "inspector::add_component_opens_the_model_dialog");
                report.Check(chosen && !fixture.scene.Components<ModelComponent>().Contains(node), "inspector::add_component_waits_for_the_chosen_model");
            }
            {
                WindowsFixture fixture(assets);
                const NodeId node = fixture.scene.AddNode("node");
                fixture.scene.Components<ModelComponent>().Emplace(node);
                fixture.selection.node = node;
                fixture.Frame();
                fixture.Frame();
                const bool chosen = fixture.ClickInSection("Model", "Browse...");
                report.Check(chosen && WindowsFixture::IsDialogOpen(), "inspector::browse_opens_the_model_dialog");
            }
            {
                WindowsFixture fixture(assets);
                const NodeId node = fixture.scene.AddNode("node");
                fixture.scene.Components<RigidBody>().Emplace(node);
                fixture.selection.node = node;
                fixture.Frame();
                fixture.Frame();
                ImGuiWindow* window = WindowsFixture::FindWindow(NodeInspectorWindow::Name);
                const ImGuiID menuButton = ImHashStr("...", 0, window->GetID("RigidBody"));
                const bool opened = fixture.ClickItem(window, menuButton);
                const bool removed = opened && fixture.ClickMenuItem("Remove");
                report.Check(removed && !fixture.scene.Components<RigidBody>().Contains(node), "inspector::section_menu_removes_the_component");
            }
            {
                WindowsFixture fixture(assets);
                fixture.selection.node = fixture.scene.AddNode("node");
                fixture.Frame();
                fixture.Frame();
                report.Check(fixture.ClickInSection("Transform", "Rotate") && fixture.gizmo.operation == GizmoOperation::Rotate, "inspector::picks_the_gizmo_operation");
                report.Check(fixture.ClickInSection("Transform", "###GizmoSpace") && fixture.gizmo.space == GizmoSpace::Local, "inspector::switches_the_gizmo_space");
            }
        }

        void RunSceneMenuTests(Report& report, const Path& assets)
        {
            const Path otherFile = assets / "scenes" / "other.scene";
            WindowsFixture fixture(assets);
            const NodeId node = fixture.scene.AddNode("node");
            fixture.edits.BeginFrame(node);
            fixture.edits.EndFrame(false);

            fixture.menu.RequestOpen(otherFile);
            fixture.Frame();
            fixture.Frame();
            report.Check(WindowsFixture::IsDialogOpen() && fixture.scene.Find(node), "menu::unsaved_changes_ask_first");
            report.Check(fixture.ClickItem(WindowsFixture::TopPopup(), "Cancel") && !WindowsFixture::IsDialogOpen() && fixture.scene.Find(node),
                "menu::cancel_keeps_the_scene");

            fixture.menu.RequestOpen(otherFile);
            fixture.Frame();
            fixture.Frame();
            report.Check(fixture.ClickItem(WindowsFixture::TopPopup(), "Don't Save") && fixture.scene.NodeCount() == 1
                    && fixture.scene.Nodes()[0].name == "other" && fixture.document.File() == otherFile,
                "menu::dont_save_replaces_the_scene");

            // Nothing to lose: no question
            fixture.menu.RequestNew();
            fixture.Frame();
            fixture.Frame();
            report.Check(!WindowsFixture::IsDialogOpen() && fixture.scene.Empty(), "menu::a_saved_scene_is_replaced_right_away");
        }

        void RunToolbarTests(Report& report)
        {
            HeadlessImGui imgui;
            bool playing = false;
            int plays = 0;
            int stops = 0;
            script::PlayRequests play;
            play.isPlaying = [&] { return playing; };
            play.playScene = [&] { ++plays; };
            play.stop = [&] { ++stops; };
            EditorToolbar toolbar;
            toolbar.SetDesc(EditorToolbarDesc { .play = &play });
            auto frame = [&] {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(1000.0f, 100.0f));
                ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar);
                if (ImGui::BeginMenuBar()) {
                    toolbar.Draw();
                    ImGui::EndMenuBar();
                }
                ImGui::End();
                ImGui::EndFrame();
            };
            frame();
            frame();
            /// Over the menu bar until the item is under the mouse, then a click.
            auto click = [&](const char* label) {
                ImGuiWindow* host = ImGui::FindWindowByName("Host");
                const ImGuiID id = ImHashStr(label, 0, host->GetID("##menubar"));
                const ImRect bar = host->MenuBarRect();
                for (float x = bar.Min.x + 1.0f; x < bar.Max.x; x += 4.0f) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddMousePosEvent(x, bar.GetCenter().y);
                    frame();
                    if (GImGui->HoveredId != id)
                        continue;
                    frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    frame();
                    return true;
                }
                return false;
            };
            report.Check(click("Play Scene") && plays == 1, "toolbar::plays_the_scene");
            playing = true;
            frame();
            report.Check(click("Stop") && stops == 1, "toolbar::stops_in_play");
        }

        void RunSceneWindowsTests(Report& report)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path root = fs::temp_directory_path(error) / "elm_scene_windows_selftest";
            fs::remove_all(root, error);
            fs::create_directories(root / "scenes", error);
            fs::create_directories(root / "models", error);
            fs::create_directories(root / "scripts", error);
            const Path assets(String(root.generic_string().c_str()));

            RunSavedMarkTests(report);
            RunNodeEditingTests(report);
            RunSceneDocumentTests(report, assets);
            RunTreeDropTests(report, assets);
            RunTreeSearchTests(report, assets);
            RunTreeEditTests(report, assets);
            RunInspectorTests(report, assets);
            RunSceneMenuTests(report, assets);
            fs::remove_all(root, error);
        }

    } // namespace

    int RunSelfTests()
    {
        Report report;
        RunHighlighterTests(report);
        RunCodeIndentTests(report);
        RunErrorLocationTests(report);
        RunEditModeTests(report);
        RunSceneEditsTests(report);
        RunSceneWindowsTests(report);
        RunToolbarTests(report);
        std::cout << "\nEditor self tests: " << report.passed << " passed, " << report.failed << " failed" << std::endl;
        return report.failed;
    }

} // namespace elm::editor
