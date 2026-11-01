#include "core/FileSystem.hpp"
#include "core/Settings.hpp"
#include "core/Threading.hpp"

#include "project/Project.hpp"

#include "resmgr/Serializer.hpp"

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
        if (auto loaded = settings.Load(settingsPath); !loaded)
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

    const Path file = project::Project::FindFile(FileSystem::Resolve(SelfTestProject));
    const auto project = resmgr::Serializer::LoadProject(file);
    if (!project) {
        std::cerr << project.error().message << std::endl;
        return 1;
    }
    return script::RunSelfTests(project->Scripts());
}
