#pragma once

#include "core/Error.hpp"

namespace elm {

    class Settings;

    /// The settings every program reads (files, log, scripts, Play), then the .settings file over them; the
    /// file and log settings take effect. Keys the program registers later still get the file's values.
    [[nodiscard]] auto LoadEngineSettings(Settings& settings) -> EngineResult<void>;

} // namespace elm
