#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include <span>

namespace elm::platform {

    /// The file of the running program.
    [[nodiscard]] auto CurrentExecutable() -> EngineResult<Path>;

    /// Starts a program and does not wait for it; it works in the current folder. An argument may not hold
    /// a '"': the arguments are passed as they are, never through a shell.
    [[nodiscard]] auto StartProcess(const Path& executable, std::span<const String> arguments) -> EngineResult<void>;

} // namespace elm::platform
