#pragma once

#include "math/Primitivs.hpp"

#include "graphics/render/CommandList.hpp"
#include "graphics/render/Render.hpp"
#include "graphics/render/Texture.hpp"

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
        struct CreateMesh {
            Handler handler;
            MeshData data;
        };
    }

    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture, command::CreateMesh>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
