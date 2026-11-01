#include "engine/EngineSettings.hpp"

#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "core/Settings.hpp"
#include "core/Unexpected.hpp"

#include "script/PlayMode.hpp"
#include "script/ScriptSystem.hpp"

namespace elm {

    auto LoadEngineSettings(Settings& settings) -> EngineResult<void>
    {
        FileSystem::RegisterSettings(settings);
        log::registerSettings(settings);
        script::ScriptSystem::RegisterSettings(settings);
        script::PlayMode::RegisterSettings(settings);
        const Path settingsPath = Settings::DefaultFilePath();
        if (auto loaded = settings.Load(settingsPath); !loaded)
            return MakeUnexpected(loaded.error());
        FileSystem::ApplySettings(settings, settingsPath.Parent());
        log::applySettings(settings, settingsPath.Parent());
        return { };
    }

} // namespace elm
