#include "core/FileSystem.hpp"
#include "core/JobSystem.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "project/Project.hpp"

#include "resource/SettingsFile.hpp"
#include "resource/Serializer.hpp"

#include "script/SelfTest.hpp"

#include <iostream>

namespace {

    /// The project the self tests run on: its tests check the demo scripts too.
    constexpr const char* SelfTestProject = "projects/Sandbox";

    /// Resolves the asset root from the settings file: no window, no render system.
    void ApplyFileSystemSettings()
    {
        elm::Settings settings;
        elm::FileSystem::RegisterSettings(settings);
        const elm::Path settingsPath = elm::Settings::DefaultFilePath();
        if (auto loaded = elm::resource::ReadSettingsFile(settings, settingsPath); !loaded)
            std::cerr << "[Warning] " << loaded.error().message << std::endl;
        elm::FileSystem::ApplySettings(settings, settingsPath.Parent());
    }

} // namespace

/// elm_tests: the binding checks and the Lua tests of the self test project, without a window or GPU.
/// The exit code is the number of failures.
int main()
{
    using namespace elm;

    ApplyFileSystemSettings();
    core::registerThread(core::ThreadRole::Update);
    // Workers as in the programs: the physics step and culling run on them
    core::JobSystem::Get().Init();

    const Path file = project::Project::FindFile(FileSystem::Resolve(SelfTestProject));
    const auto project = resource::Serializer::LoadProject(file);
    if (!project) {
        std::cerr << project.error().message << std::endl;
        return 1;
    }
    const int failures = script::RunSelfTests(project->Scripts());
    core::JobSystem::Get().Shutdown();
    return failures;
}
