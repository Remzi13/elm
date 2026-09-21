#pragma once

#include "math/Primitivs.hpp"

#include "graphics/render/CommandList.hpp"
#include "graphics/render/Render.hpp"
#include "graphics/render/Texture.hpp"

namespace elm {
namespace render {

    namespace command {
        struct CreateTexture {
            core::Handler handler;
            TextureInfo info;
        };
        struct UploadTexture {
            core::Handler handler;
            TextureData data;
        };
        struct CreateMesh {
            core::Handler handler;
            core::Handler meshData;
        };
        struct DestroyMesh {
            core::Handler handler;
        };
    }

    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture, command::CreateMesh, command::DestroyMesh>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
