#pragma once

#include "core/Error.hpp"

#include <cstdint>
#include <memory>

namespace elm {

class RenderDevice {
public:
    RenderDevice() = default;
    virtual ~RenderDevice() = default;

    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;

    [[nodiscard]] virtual auto Initialize() -> EngineResult<void> = 0;
    virtual void Shutdown() = 0;

    [[nodiscard]] virtual void* NativeHandle() const noexcept = 0;
};

} // namespace elm
