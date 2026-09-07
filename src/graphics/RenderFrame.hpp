#pragma once

#include <cstdint>

namespace elm {

class RenderContext;

class RenderFrame {
public:
    RenderFrame() = default;
    virtual ~RenderFrame() = default;

    RenderFrame(const RenderFrame&) = delete;
    RenderFrame& operator=(const RenderFrame&) = delete;

    [[nodiscard]] virtual RenderContext* GetContext() const noexcept = 0;
    virtual void Submit() = 0;
};

} // namespace elm
