#include "resource/ProgramSettings.hpp"

#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "core/Settings.hpp"

#include "resource/SettingsFile.hpp"

namespace elm::resource {

    auto LoadProgramSettings(Settings& settings) -> EngineResult<void>
    {
        FileSystem::RegisterSettings(settings);
        log::registerSettings(settings);
        const Path settingsPath = Settings::DefaultFilePath();
        if (auto loaded = ReadSettingsFile(settings, settingsPath); !loaded)
            return loaded;
        FileSystem::ApplySettings(settings, settingsPath.Parent());
        log::applySettings(settings, settingsPath.Parent());
        return {};
    }

} // namespace elm::resource
