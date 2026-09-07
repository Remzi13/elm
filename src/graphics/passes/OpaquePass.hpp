#pragma once

#include "graphics/RenderFrame.hpp"
#include "graphics/RenderWorld.hpp"

namespace elm {

class OpaquePass {
public:
    virtual ~OpaquePass() = default;
    virtual void Execute(const RenderWorld& world, RenderFrame& frame) = 0;
};

} // namespace elm
