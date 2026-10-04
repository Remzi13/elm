#pragma once


#include "graphics/render/Texture.hpp"

#include <cstdint>

namespace elm {
namespace render {

    namespace command::resource {
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
        struct ResizeMainSwapChain {
            uint32_t width;
            uint32_t height;
        };
    }

} // namespace render
} // namespace elm
