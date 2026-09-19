#pragma once

#include "graphics/render/Render.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/CommandList.hpp"

namespace elm {
namespace render {
    
    struct UploadTexture {
        Handler handler;
        TextureData data;
    };

    using RenderCommandVariant = std::variant<UploadTexture>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
