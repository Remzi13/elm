#pragma once

#include "core/Error.hpp"

#include <cstdint>

namespace elm {

class RenderFrame;
class RenderWorld;

class Renderer {
public:
    virtual ~Renderer() = default;

    [[nodiscard]] virtual auto Initialize(uint32_t width, uint32_t height, StringView title) -> EngineResult<void> = 0;
    [[nodiscard]] virtual auto BeginFrame() -> RenderFrame* = 0;
    virtual void RenderWorld(const RenderWorld& world, RenderFrame& frame) = 0;
    virtual void EndFrame(RenderFrame& frame) = 0;
    virtual void Shutdown() = 0;
};

} // namespace elm
