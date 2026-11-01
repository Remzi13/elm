#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/Log.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "engine/Engine.hpp"
#include "engine/PlayerLayer.hpp"

#include "editor/EditorLayer.hpp"

#include "platform/Process.hpp"

#include "project/Project.hpp"

#include "resmgr/ProjectBuilder.hpp"
#include "resmgr/Serializer.hpp"

#include "script/ScriptSystem.hpp"

#include <algorithm>
#include <optional>

namespace {

    constexpr elm::EngineDesc MainWindow { 1280, 720, "C++23 3D Engine - Direct3D 12 & Jolt Physics" };
    constexpr const char* DefaultScriptApiFile = "projects/Sandbox/assets/scripts/.meta/elm.lua";

    /// Resolves the asset root from the settings file without starting the engine.
    void ApplyFileSystemSettings()
    {
        elm::Settings settings;
        elm::FileSystem::RegisterSettings(settings);
        const elm::Path settingsPath = elm::Settings::DefaultFilePath();
        if (auto loaded = settings.Load(settingsPath); !loaded)
            std::cerr << "[Warning] " << loaded.error().message << std::endl;
        elm::FileSystem::ApplySettings(settings, settingsPath.Parent());
    }

    /// --dump-script-api <file>: writes the LuaLS definitions of the engine's Lua API.
    int DumpScriptApi(const char* fileName)
    {
        using namespace elm;
        const auto meta = script::ScriptSystem::GenerateApiMeta();
        if (!meta) {
            std::cerr << meta.error().message << std::endl;
            return 1;
        }
        const Path path = FileSystem::Resolve(fileName);
        std::error_code error;
        FileSystem::CreateDirectories(path.Parent(), error);
        auto stream = FileSystem::OpenForWriting(path, std::ios::out | std::ios::binary);
        stream.write(meta->data(), static_cast<std::streamsize>(meta->size()));
        if (!stream) {
            std::cerr << "Cannot write " << path.string() << std::endl;
            return 1;
        }
        std::cout << "Script API written to " << path.string() << std::endl;
        return 0;
    }

    /// --build <project>: builds the project into its Build/<Name> folder with this program (docs/project/Проекты.md#Сборка).
    int BuildGame(const char* folderOrFile)
    {
        using namespace elm;
        const auto project = resmgr::Serializer::LoadProject(project::Project::FindFile(FileSystem::Resolve(folderOrFile)));
        const auto executable = platform::CurrentExecutable();
        if (!project || !executable) {
            std::cerr << (project ? executable.error().message : project.error().message) << std::endl;
            return 1;
        }
        const auto folder = resmgr::ProjectBuilder::Build(*project, { *executable, FileSystem::Resolve("shaders") });
        if (!folder) {
            std::cerr << folder.error().message << std::endl;
            return 1;
        }
        std::cout << "Game built in " << folder->string() << std::endl;
        return 0;
    }

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

    /// The frame loop, then Shutdown: it runs while the layer is still alive.
    int RunAndShutdown(elm::Engine& engine)
    {
        const auto runResult = engine.Run();
        engine.Shutdown();
        if (!runResult)
            return ReportFatal("Engine runtime failure", runResult.error());
        LOG_MESSAGE( elm::log::Category::Core, "Main", "Application terminated gracefully." );
        return 0;
    }

    /// The game alone, no editor (docs/project/Проекты.md#Плеер).
    int RunPlayer(const elm::Path& project, int argc, char** argv)
    {
        elm::Engine engine;
        elm::PlayerLayer player(engine);
        if (const auto init = engine.Init(MainWindow, player); !init)
            return ReportFatal("Failed to initialize engine", init.error());
        if (const auto opened = engine.OpenProject(project); !opened) {
            engine.Shutdown();
            return ReportFatal("Failed to open the project", opened.error());
        }
        LoadTestSceneArgument(engine, argc, argv);
        return RunAndShutdown(engine);
    }

    /// The editor with the --project argument, otherwise the project opened last time, otherwise the Project Hub.
    int RunEditor(const std::optional<elm::Path>& projectArgument, int argc, char** argv)
    {
        using namespace elm;
        Engine engine;
        EditorLayer editor(engine);
        if (const auto init = engine.Init(MainWindow, editor); !init)
            return ReportFatal("Failed to initialize engine", init.error());

        const Path projectPath = projectArgument ? *projectArgument : editor.LastProject();
        const auto opened = projectPath.Empty() ? EngineResult<void> { } : engine.OpenProject(projectPath);
        if (!opened && projectArgument) {
            engine.Shutdown();
            return ReportFatal("Failed to open the project", opened.error());
        }
        if (!opened) {
            WARNING_MESSAGE(log::Category::Core, "Main", "Failed to open last project \"%s\": %s",
                projectPath.string().c_str(), opened.error().message.c_str());
        }
        // The last project may be gone: the hub offers the others
        if (projectPath.Empty() || !opened)
            editor.ShowProjectHub();

        LoadTestSceneArgument(engine, argc, argv);
        return RunAndShutdown(engine);
    }

} // namespace

int main(int argc, char** argv) {
    using namespace elm;

    // Tool modes: run without a window and exit
    for (int i = 1; i < argc; ++i) {
        const bool dumpApi = std::strcmp(argv[i], "--dump-script-api") == 0;
        const bool build = std::strcmp(argv[i], "--build") == 0;
        if (!dumpApi && !build)
            continue;
        ApplyFileSystemSettings();
        core::registerThread(core::ThreadRole::Update);
        if (build)
            return i + 1 < argc ? BuildGame(argv[i + 1]) : 1;
        return DumpScriptApi(i + 1 < argc ? argv[i + 1] : DefaultScriptApiFile);
    }

    LOG_MESSAGE( log::Category::Core, "Main", "Starting C++23 Cross-Platform 3D Engine Core..." );
    
    // A built game plays its own project, from its own folder: its .settings and shaders are there
    const auto builtGame = FindBuiltGame();
    if (builtGame) {
        std::error_code error;
        FileSystem::SetCurrentPath(builtGame->Parent(), error);
    }
    // --player: the game alone, no editor (docs/project/Проекты.md#Плеер)
    const bool player = builtGame
        || std::any_of(argv + 1, argv + argc, [](const char* argument) { return std::strcmp(argument, "--player") == 0; });
    const auto projectFlag = FindArgument(argc, argv, "--project");
    const std::optional<Path> projectArgument = projectFlag ? std::optional<Path>(Path(*projectFlag)) : builtGame;
    if (player && !projectArgument) {
        std::cerr << "[Fatal Error] The player needs a project: --project <folder or .elmproject file>" << std::endl;
        return 1;
    }
    return player ? RunPlayer(*projectArgument, argc, argv) : RunEditor(projectArgument, argc, argv);
}
