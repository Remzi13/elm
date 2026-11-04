#include "core/ProgramSettings.hpp"

#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "core/Settings.hpp"
#include "core/Unexpected.hpp"

namespace elm {

    auto LoadProgramSettings(Settings& settings) -> EngineResult<void>
    {
        FileSystem::RegisterSettings(settings);
        log::registerSettings(settings);
        const Path settingsPath = Settings::DefaultFilePath();
        if (auto loaded = settings.Load(settingsPath); !loaded)
            return MakeUnexpected(loaded.error());
        FileSystem::ApplySettings(settings, settingsPath.Parent());
        log::applySettings(settings, settingsPath.Parent());
        return {};
    }

} // namespace elm
