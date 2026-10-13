#pragma once

#include "core/FileSystem.hpp"

namespace elm {

/// Locations of the engine asset folders.
class AssetPaths {
public:
    AssetPaths() = delete;

    /// The "assets" folder under FileSystem::RootPath().
    [[nodiscard]] static auto Root() -> Path;
    [[nodiscard]] static auto Models() -> Path;
    [[nodiscard]] static auto Scenes() -> Path;
    [[nodiscard]] static auto Scripts() -> Path;
};

} // namespace elm
