#pragma once

#include "graphics/RenderFrame.hpp"
#include "graphics/RenderWorld.hpp"
#include "graphics/VisibilitySystem.hpp"

namespace elm {

class SceneRenderer {
public:
    virtual ~SceneRenderer() = default;

    virtual void RenderVisibility(const RenderWorld& world, RenderFrame& frame) = 0;
    virtual void RenderDepth(const RenderWorld& world, RenderFrame& frame) = 0;
    virtual void RenderOpaque(const RenderWorld& world, RenderFrame& frame) = 0;
    virtual void RenderTransparent(const RenderWorld& world, RenderFrame& frame) = 0;
    virtual void RenderDebug(const RenderWorld& world, RenderFrame& frame) = 0;
};

} // namespace elm
