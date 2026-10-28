#pragma once

#include "core/Std.hpp"

#include "project/AssetPath.hpp"

#include <optional>

namespace elm::project {

    /// Where the game starts: its startup scene, then its main module (docs/project/Проекты.md).
    /// At least one of them is set. Owned by ProjectDesc.
    struct ProjectEntry {
        /// The scene the game starts in; none: the main module builds the scene.
        std::optional<AssetPath> startupScene;
        /// Lua module with the main.on_* hooks, a dotted name under the scripts folder; empty: no main module.
        String mainModule;
    };

    /// Contents of a <Name>.elmproject file (docs/project/Проекты.md).
    struct ProjectDesc {
        /// Name of the game, the same as the project folder.
        String name;
        ProjectEntry entry;
    };

} // namespace elm::project
