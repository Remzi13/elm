#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/File.hpp"
#include "core/Log.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "editor/Editor.hpp"
#include "editor/EditorSelfTest.hpp"

#include "project/Project.hpp"

#include "resource/SettingsFile.hpp"
#include "resource/ProjectBuilder.hpp"
#include "resource/Serializer.hpp"

#include "script/ScriptSystem.hpp"

#include <optional>

// editor: the editor (docs/editor/Редактор.md), and its tools without a window: --build, --dump-script-api,
// --editor-selftest. The game alone is the program elm next to it.

namespace {

    constexpr const char* DefaultScriptApiFile = "projects/Sandbox/assets/scripts/.meta/elm.lua";

    /// Resolves the asset root from the settings file without starting the systems.
    void ApplyFileSystemSettings()
    {
        elm::Settings settings;
        elm::FileSystem::RegisterSettings(settings);
        const elm::Path settingsPath = elm::Settings::DefaultFilePath();
        if (auto loaded = elm::resource::ReadSettingsFile(settings, settingsPath); !loaded)
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
        std::error_code folderError;
        FileSystem::CreateDirectories(path.Parent(), folderError);
        File file;
        if (!file.Open(path, FileMode::Write) || !file.Write(*meta) || !file.Close()) {
            std::cerr << "Cannot write " << path.string() << std::endl;
            return 1;
        }
        std::cout << "Script API written to " << path.string() << std::endl;
        return 0;
    }

    /// --build <project>: builds the project into its Build/<Name> folder with the program elm (docs/project/Проекты.md#Сборка).
    int BuildGame(const char* folderOrFile)
    {
        using namespace elm;
        const auto project = resource::Serializer::LoadProject(project::Project::FindFile(FileSystem::Resolve(folderOrFile)));
        const auto executable = Editor::FindPlayerExecutable();
        if (!project || !executable) {
            std::cerr << (project ? executable.error().message : project.error().message) << std::endl;
            return 1;
        }
        const auto folder = resource::ProjectBuilder::Build(*project, { *executable, FileSystem::Resolve("shaders") });
        if (!folder) {
            std::cerr << folder.error().message << std::endl;
            return 1;
        }
        std::cout << "Game built in " << folder->string() << std::endl;
        return 0;
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

    int ReportFatal(const char* what, const elm::EngineError& error)
    {
        std::cerr << "[Fatal Error] " << what << ": " << error.message << std::endl;
        return 1;
    }

    /// The editor with the --project argument, otherwise the project opened last time, otherwise the Project Hub.
    int RunEditor(const std::optional<elm::Path>& projectArgument, int argc, char** argv)
    {
        using namespace elm;
        Editor editor;
        if (const auto init = editor.Init(); !init)
            return ReportFatal("Failed to initialize the editor", init.error());

        const Path projectPath = projectArgument ? *projectArgument : editor.LastProject();
        const auto opened = projectPath.Empty() ? EngineResult<void> {} : editor.OpenProject(projectPath);
        if (!opened && projectArgument)
            return ReportFatal("Failed to open the project", opened.error());
        if (!opened) {
            WARNING_MESSAGE(log::Category::Core, "Main", "Failed to open last project \"%s\": %s",
                projectPath.string().c_str(), opened.error().message.c_str());
        }
        // The last project may be gone: the hub offers the others
        if (projectPath.Empty() || !opened)
            editor.ShowProjectHub();

        editor.Run();
        editor.Shutdown();
        LOG_MESSAGE(log::Category::Core, "Main", "Application terminated gracefully.");
        return 0;
    }

} // namespace

int main(int argc, char** argv)
{
    using namespace elm;

    // Tool modes: run without a window and exit
    for (int i = 1; i < argc; ++i) {
        const bool dumpApi = std::strcmp(argv[i], "--dump-script-api") == 0;
        const bool build = std::strcmp(argv[i], "--build") == 0;
        const bool editorSelfTest = std::strcmp(argv[i], "--editor-selftest") == 0;
        if (!dumpApi && !build && !editorSelfTest)
            continue;
        ApplyFileSystemSettings();
        core::registerThread(core::ThreadRole::Update);
        if (build)
            return i + 1 < argc ? BuildGame(argv[i + 1]) : 1;
        // The editor's own logic: elm_tests checks the engine without the editor
        if (editorSelfTest)
            return editor::RunSelfTests();
        return DumpScriptApi(i + 1 < argc ? argv[i + 1] : DefaultScriptApiFile);
    }

    LOG_MESSAGE(log::Category::Core, "Main", "Starting editor...");

    const auto projectFlag = FindArgument(argc, argv, "--project");
    const std::optional<Path> projectArgument = projectFlag ? std::optional<Path>(Path(*projectFlag)) : std::nullopt;
    return RunEditor(projectArgument, argc, argv);
}
