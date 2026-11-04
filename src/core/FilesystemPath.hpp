#pragma once

#include <filesystem>

namespace elm {

    class Path;

    /// The path as the standard library takes it, with its UTF-8 names intact on every platform.
    /// For the code of core that works with the disk: FileSystem.hpp keeps <filesystem> out of the rest of the engine.
    [[nodiscard]] auto ToFilesystemPath(const Path& path) -> std::filesystem::path;

} // namespace elm
