#pragma once


#include "graphics/render/CommandList.hpp"
#include "graphics/render/Texture.hpp"

#include <cstdint>

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
        struct ResizeMainSwapChain {
            uint32_t width;
            uint32_t height;
        };
        struct ResizeEngineViewport {
            uint32_t width;
            uint32_t height;
        };
    }

    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture, command::DestroyTexture,
        command::CreateMesh, command::DestroyMesh, command::ResizeMainSwapChain, command::ResizeEngineViewport>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
