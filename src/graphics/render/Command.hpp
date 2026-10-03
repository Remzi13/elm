#pragma once


#include "graphics/render/CommandList.hpp"
#include "graphics/render/Texture.hpp"

#include <cstdint>

namespace elm {
namespace render {

    using RenderSurfaceId = uint32_t;

    struct RenderVertex {
        float position[2];
        float uv[2];
        float color[4];
    };

    struct ScissorRect {
        uint32_t left;
        uint32_t top;
        uint32_t right;
        uint32_t bottom;
    };

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
        struct CreateRenderSurface {
            RenderSurfaceId id;
            uint32_t width;
            uint32_t height;
            void* nativeHandle;
            void* nativeDisplay;
        };
        struct ResizeRenderSurface {
            RenderSurfaceId id;
            uint32_t width;
            uint32_t height;
        };
        struct DestroyRenderSurface {
            RenderSurfaceId id;
        };
        struct BeginRenderPass {
            RenderSurfaceId surface;
            uint32_t width;
            uint32_t height;
            bool clear;
        };
        struct DrawIndexed {
            RenderSurfaceId surface;
            Vector<RenderVertex> vertices;
            Vector<uint32_t> indices;
            core::Handler texture;
            ScissorRect scissor;
        };
        struct EndRenderPass {
            RenderSurfaceId surface;
            bool present;
        };
    }

    using RenderCommandVariant = std::variant<command::UploadTexture, command::CreateTexture, command::DestroyTexture,
        command::CreateMesh, command::DestroyMesh, command::ResizeMainSwapChain, command::ResizeEngineViewport,
        command::CreateRenderSurface, command::ResizeRenderSurface, command::DestroyRenderSurface,
        command::BeginRenderPass, command::DrawIndexed, command::EndRenderPass>;

    using CommandList = BasicCommandList<RenderCommandVariant>;

} // namespace render
} // namespace elm
