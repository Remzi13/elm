#pragma once

#include "script/PlayMode.hpp"

#include <optional>

namespace elm::resource {
    class ResourceSystem;
}

namespace elm {

    /// How Play starts in the open project, for PlayMode::SetStartSource: the startup scene for Play Project,
    /// the project's main module. Nothing without a project or when the startup scene cannot be read (logged):
    /// the Play does not begin.
    [[nodiscard]] std::optional<script::PlayStart> MakeProjectPlayStart(const resource::ResourceSystem& resources, script::PlayTarget target);

} // namespace elm
