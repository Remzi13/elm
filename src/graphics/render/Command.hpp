#pragma once


#include "graphics/render/CommandList.hpp"
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
        struct DestroyTexture {
            core::Handler handler;
        };
        struct CreateMesh {
            core::Handler handler;
            core::Handler meshData;
        };
        struct DestroyMesh {
            core::Handler handler;
        };
    }

    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture, command::DestroyTexture, command::CreateMesh, command::DestroyMesh>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
