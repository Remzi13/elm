#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/Log.hpp"

#include "engine/Engine.hpp"

#include "graphics/debug/DebugDrawing.hpp"
#if defined(ELM_DEBUG_DRAW)
#include "graphics/debug/ColliderVisualizer.hpp"
#include "graphics/debug/PhysicsVisualizer.hpp"
#endif

#include "platform/Process.hpp"

#include "project/Project.hpp"

#include <optional>

// elm: the game alone, without the editor (docs/project/Проекты.md#Плеер). `elm --project <folder or
// .elmproject file>`; a built game plays the project next to it with no arguments. It runs the engine's
// frame itself; with ELM_DEBUG_DRAW it has debug drawing.

namespace {

    constexpr elm::EngineDesc MainWindow { 1280, 720, "elm" };

    /// A built game (docs/project/Проекты.md#Сборка): <Name>.elmproject lies next to the program <Name>(.exe).
    std::optional<elm::Path> FindBuiltGame()
    {
        const auto executable = elm::platform::CurrentExecutable();
        if (!executable)
            return std::nullopt;
        elm::Path file = executable->Parent() / executable->Stem();
        file += elm::project::Project::FileExtension;
        if (!elm::FileSystem::Exists(file))
            return std::nullopt;
        return file;
    }

    /// The value after --name, if the argument is given.
    std::optional<const char*> FindArgument(int argc, char** argv, const char* name)
    {
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], name) == 0)
                return argv[i + 1];
        }
        return std::nullopt;
    }

    /// --scene <preset index> [--instances <count>]: a test scene in place of the startup scene.
    void LoadTestSceneArgument(elm::Engine& engine, int argc, char** argv)
    {
        int scenePreset = -1;
        uint32_t instanceCount = 1500;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--scene") == 0)
                scenePreset = std::atoi(argv[i + 1]);
            else if (std::strcmp(argv[i], "--instances") == 0)
                instanceCount = static_cast<uint32_t>(std::atoi(argv[i + 1]));
        }
        if (scenePreset >= 0)
            engine.LoadTestScene(static_cast<elm::ScenePreset>(scenePreset), instanceCount);
    }

    int ReportFatal(const char* what, const elm::EngineError& error)
    {
        std::cerr << "[Fatal Error] " << what << ": " << error.message << std::endl;
        return static_cast<int>(error.code);
    }

    /// The debug drawing of a build with ELM_DEBUG_DRAW, with the editor's physics visualizers (channels
    /// Physics/Colliders, Triggers, Raycasts, Contacts, on and off in .settings); none without it.
    elm::UniquePtr<elm::graphics::DebugDrawing> MakeDebugDraw([[maybe_unused]] elm::Engine& engine)
    {
#if defined(ELM_DEBUG_DRAW)
        using namespace elm::graphics;
        auto debugDraw = elm::MakeUnique<DebugDrawing>(engine.GetSettings());
        debugDraw->AddVisualizer(elm::MakeUnique<ColliderVisualizer>(engine.GetScene(), debugDraw->GetDebugDraw()));
        debugDraw->AddVisualizer(elm::MakeUnique<PhysicsVisualizer>(engine.GetPhysics().FrameLog(), debugDraw->GetDebugDraw()));
        return debugDraw;
#else
        return nullptr;
#endif
    }

    /// The game: Play Project, the scene in the whole window, until the window closes or the scripts stop it.
    void RunGame(elm::Engine& engine, elm::graphics::DebugDrawing* debugDraw)
    {
        elm::script::PlayMode& play = engine.GetPlayMode();
        play.RequestPlay(elm::script::PlayTarget::Project);
        while (!engine.ShouldClose()) {
            // No Edit mode to go back to: Stop (on_stop still runs) or a Play that could not start ends the game
            const bool stopRequested = play.GetRequest() == elm::script::PlayMode::Request::Stop;
            play.ApplyRequest();
            if (stopRequested || !play.IsPlaying())
                return;

            const float deltaTime = engine.BeginFrame();
            // Before anything of the frame draws, physics steps included
            if (debugDraw)
                debugDraw->BeginFrame(deltaTime);
            engine.Simulate(deltaTime);

            elm::render::FrameWriter frame = engine.BeginRenderFrame();
            if (!frame)
                return;
            engine.GetGraphics().PresentToWindow(frame, engine.GetWindow().GetFramebufferSize());
            engine.DrawScene(frame);
            if (debugDraw)
                debugDraw->EndFrame(frame);
            engine.SubmitFrame(frame);
        }
    }

} // namespace

int main(int argc, char** argv)
{
    using namespace elm;

    LOG_MESSAGE( log::Category::Core, "Main", "Starting elm..." );

    // A built game plays its own project, from its own folder: its .settings and shaders are there
    const auto builtGame = FindBuiltGame();
    if (builtGame) {
        std::error_code error;
        FileSystem::SetCurrentPath(builtGame->Parent(), error);
    }
    const auto projectFlag = FindArgument(argc, argv, "--project");
    const std::optional<Path> project = projectFlag ? std::optional<Path>(Path(*projectFlag)) : builtGame;
    if (!project) {
        std::cerr << "[Fatal Error] elm needs a project: --project <folder or .elmproject file>" << std::endl;
        return 1;
    }

    Engine engine;
    // A game does not watch its script files unless its .settings asks for it (the editor's does)
    engine.GetSettings().Set(Settings::Category::Script, script::ScriptSystem::HotReloadSetting, false);
    // Made before Init: the scripts get its DebugDraw. Shutdown below ends the scripts while it lives.
    const auto debugDraw = MakeDebugDraw(engine);
    EngineDesc desc { MainWindow };
    desc.debugDraw = debugDraw ? &debugDraw->GetDebugDraw() : nullptr;
    if (const auto init = engine.Init(desc); !init)
        return ReportFatal("Failed to initialize engine", init.error());
    auto opened = resource::ResourceSystem::ReadProject(*project);
    if (!opened) {
        engine.Shutdown();
        return ReportFatal("Failed to open the project", opened.error());
    }
    engine.GetWindow().SetTitle(opened->Name());
    engine.OpenProject(std::move(*opened));
    LoadTestSceneArgument(engine, argc, argv);

    RunGame(engine, debugDraw.get());
    engine.Shutdown();
    LOG_MESSAGE( log::Category::Core, "Main", "Application terminated gracefully." );
    return 0;
}
