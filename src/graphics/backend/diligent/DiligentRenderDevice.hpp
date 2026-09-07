#pragma once

#include "graphics/RenderDevice.hpp"

namespace elm::diligent_backend {

class DiligentRenderDevice final : public RenderDevice {
public:
    DiligentRenderDevice();
    ~DiligentRenderDevice() override;

    [[nodiscard]] auto Initialize() -> EngineResult<void> override;
    void Shutdown() override;
    [[nodiscard]] void* NativeHandle() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace elm::diligent_backend
