#pragma once

#include "graphics/render/Render.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/CommandList.hpp"

namespace elm {
namespace render {

    namespace command {
    struct CreateTexture {
        Handler handler;
        TextureInfo info;
    };
    struct UploadTexture {
        Handler handler;
        TextureData data;
    };
    }


    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
