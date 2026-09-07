#pragma once

#include "graphics/RenderContext.hpp"

#include <memory>

namespace elm::diligent_backend {

class DiligentRenderContext final : public RenderContext {
public:
    DiligentRenderContext();
    ~DiligentRenderContext() override;

    [[nodiscard]] RenderDevice* GetDevice() const noexcept override;
    [[nodiscard]] void* NativeHandle() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace elm::diligent_backend