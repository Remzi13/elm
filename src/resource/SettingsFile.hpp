#pragma once

#include "core/Error.hpp"

namespace elm {
    class Path;
    class Settings;
} // namespace elm

namespace elm::resource {

    /// Reads a .settings file (JSON) into the settings. A missing file changes nothing; a value that does not fit
    /// its registered key is an error and changes nothing either. Errors go to the log. Any thread.
    [[nodiscard]] auto ReadSettingsFile(Settings& settings, const Path& path) -> EngineResult<void>;
    /// Writes the registered values and the file's values whose keys nobody registered. Replaces the file.
    [[nodiscard]] auto WriteSettingsFile(const Settings& settings, const Path& path) -> EngineResult<void>;

} // namespace elm::resource
