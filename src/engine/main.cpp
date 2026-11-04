#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/Log.hpp"

#include "engine/Game.hpp"

#include "platform/Process.hpp"

#include "project/Project.hpp"

#include <optional>

// elm: the game alone, without the editor (docs/project/Проекты.md#Плеер). `elm --project <folder or
// .elmproject file>`; a built game plays the project next to it with no arguments. The program is Game;
// with ELM_DEBUG_DRAW it has debug drawing.

namespace {

    constexpr elm::GameDesc MainWindow { { 1280, 720 }, "elm" };

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
    void LoadTestSceneArgument(elm::Game& game, int argc, char** argv)
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
            game.LoadTestScene(static_cast<elm::ScenePreset>(scenePreset), instanceCount);
    }

    int ReportFatal(const char* what, const elm::EngineError& error)
    {
        std::cerr << "[Fatal Error] " << what << ": " << error.message << std::endl;
        return static_cast<int>(error.code);
    }

} // namespace

int main(int argc, char** argv)
{
    using namespace elm;

    LOG_MESSAGE(log::Category::Core, "Main", "Starting elm...");

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

    Game game;
    if (const auto init = game.Init(MainWindow); !init)
        return ReportFatal("Failed to initialize the game", init.error());
    if (const auto opened = game.OpenProject(*project); !opened)
        return ReportFatal("Failed to open the project", opened.error());
    LoadTestSceneArgument(game, argc, argv);

    game.Run();
    game.Shutdown();
    LOG_MESSAGE(log::Category::Core, "Main", "Application terminated gracefully.");
    return 0;
}
