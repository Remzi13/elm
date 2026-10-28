#pragma once

#include "graphics/Camera.hpp"

#include "render/OverlayFrame.hpp"
#include "render/ViewPort.hpp"

namespace elm::graphics {

    // The game alone in the main window: the player (docs/project/Проекты.md#Плеер) has no UI. The scene
    // view fills the window's framebuffer and the overlay pass draws it there as one textured quad, the
    // way the editor's viewport window shows it. Update thread.

    /// Fits the scene view and the camera's aspect to the window's framebuffer; an empty one (minimized) changes nothing.
    void FitGameView(Size framebuffer, render::ViewPort& viewPort, Camera& camera);

    /// The overlay of the frame: the scene view over the whole main surface.
    void WriteGameOverlay(const render::ViewPort& viewPort, Size framebuffer, render::OverlayFrame& overlay);

} // namespace elm::graphics
