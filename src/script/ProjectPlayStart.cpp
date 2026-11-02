#include "script/ProjectPlayStart.hpp"

#include "core/Log.hpp"

#include "project/Project.hpp"

#include "resource/ResourceSystem.hpp"

namespace elm::script {

    namespace {

        constexpr const char* LogSource = "Play";

    } // namespace

    std::optional<PlayStart> MakeProjectPlayStart(const resource::ResourceSystem& resources, PlayTarget target)
    {
        const project::Project* project = resources.GetProject();
        if (!project)
            return std::nullopt;
        PlayStart start;
        start.mainModule = project->Desc().entry.mainModule;
        if (target == PlayTarget::Scene || !project->Desc().entry.startupScene)
            return start;
        auto scene = resources.LoadStartupScene();
        if (!scene) {
            ERROR_MESSAGE(log::Category::Core, LogSource, "Play Project did not start: %s", scene.error().message.c_str());
            return std::nullopt;
        }
        start.scene = std::move(*scene);
        return start;
    }

} // namespace elm::script
