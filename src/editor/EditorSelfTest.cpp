#include "editor/EditorSelfTest.hpp"

#include "editor/EditMode.hpp"
#include "editor/script/CodeIndent.hpp"
#include "editor/script/ErrorLocation.hpp"
#include "editor/script/LuaHighlighter.hpp"

#include "model/ModelCatalog.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneSystem.hpp"
#include "Scene/ScriptComponent.hpp"

#include "physics/PhysicsSystem.hpp"

#include "resource/ResourceSystem.hpp"


#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

#include "math/Primitivs.hpp"

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

    } // namespace

    int RunSelfTests()
    {
        Report report;
        RunHighlighterTests(report);
        RunCodeIndentTests(report);
        RunErrorLocationTests(report);
        RunEditModeTests(report);
        std::cout << "\nEditor self tests: " << report.passed << " passed, " << report.failed << " failed" << std::endl;
        return report.failed;
    }

} // namespace elm::editor
