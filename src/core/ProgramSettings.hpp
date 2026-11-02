#pragma once

#include "core/Error.hpp"

namespace elm {

    class Settings;

    /// The settings of files and the log, then the .settings file over all registered ones; the file and log
    /// settings take effect. Keys the program registers later still get the file's values.
    [[nodiscard]] auto LoadProgramSettings(Settings& settings) -> EngineResult<void>;

} // namespace elm
