#pragma once

#include "graphics/RenderFrame.hpp"
#include "graphics/RenderWorld.hpp"

namespace elm {

class VisibilityPass {
public:
    virtual ~VisibilityPass() = default;
    virtual void Execute(const RenderWorld& world, RenderFrame& frame) = 0;
};

} // namespace elm
