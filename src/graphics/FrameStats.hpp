#pragma once

#include "Scene/Transform.hpp"

#include <cstdint>

namespace elm {

    /// Per-frame engine statistics shown by the debug UI.
    struct FrameStats {
        float fps { 0.0f };
        float deltaTimeMs { 0.0f };
        uint32_t physicsBodyCount { 0 };
        Transform boxTransform;
        Transform groundTransform;
    };

} // namespace elm
