#pragma once

#include "graphics/RenderResourceManager.hpp"

namespace elm::diligent_backend {

class DiligentTexture final : public RenderTexture {
public:
    DiligentTexture() = default;
};

} // namespace elm::diligent_backend