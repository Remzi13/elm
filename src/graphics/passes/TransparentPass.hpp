#pragma once

#include "graphics/RenderFrame.hpp"
#include "graphics/RenderWorld.hpp"

namespace elm {

class TransparentPass {
public:
    virtual ~TransparentPass() = default;
    virtual void Execute(const RenderWorld& world, RenderFrame& frame) = 0;
};

} // namespace elm
