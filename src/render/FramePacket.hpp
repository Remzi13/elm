#pragma once

#include "render/RenderSystem.hpp"
#include "render/OverlayFrame.hpp"

namespace elm {

/// Thread-safe frame data packet for pipelined Update/Render.
/// Update thread fills this, then hands it off to the render thread.
struct FramePacket {
    render::FrameData   frameData;
    render::FrameStats  stats;
    render::OverlayFrame overlay;
    float       deltaTime { 0.0f };
};

} // namespace elm
