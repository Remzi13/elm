#pragma once

#include <cstdint>

namespace elm {

class RenderDevice;

class RenderContext {
public:
    RenderContext() = default;
    virtual ~RenderContext() = default;

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    [[nodiscard]] virtual RenderDevice* GetDevice() const noexcept = 0;
    [[nodiscard]] virtual void* NativeHandle() const noexcept = 0;
};

} // namespace elm
