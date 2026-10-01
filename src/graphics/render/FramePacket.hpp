#pragma once

#include "graphics/ImGuiSystem.hpp"
#include "graphics/RenderSystem.hpp"

namespace elm {

/// Thread-safe frame data packet for pipelined Update/Render.
/// Update thread fills this, then hands it off to the render thread.
struct FramePacket {
    FrameData   frameData;
    FrameStats  stats;
    ImGuiFrame  ui;
    float       deltaTime { 0.0f };
};

} // namespace elm
