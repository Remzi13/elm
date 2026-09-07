#pragma once

#include "graphics/RenderFrame.hpp"
#include "graphics/RenderWorld.hpp"

namespace elm {

class DepthPass {
public:
    virtual ~DepthPass() = default;
    virtual void Execute(const RenderWorld& world, RenderFrame& frame) = 0;
};

} // namespace elm
