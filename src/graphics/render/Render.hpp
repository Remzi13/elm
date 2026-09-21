#pragma once

#include <compare>

#include "core/Handler.hpp"

namespace elm {
namespace render {   

    struct Mesh {
        core::Handler vb;
        core::Handler ib;
        uint32_t indexCount { 0 };

        bool operator==(const Mesh& o) const
        {
            return vb == o.vb && ib == o.ib && indexCount == o.indexCount;
        }
    };

    enum BufferType {
        VertexBuffer,
        IndexBuffer,
        UniformBuffer
    };

    enum TextureFormat {
        RGBA8_UNORM,
        RGBA8_UNORM_SRGB,
        R32_FLOAT,
        D32_FLOAT
    };

    enum TextureUsage {
        Default,
        Immutable,
        Dynamic
    };

    enum TextureBindFlags {
        BindShaderResource = 1 << 0,
        BindRenderTarget = 1 << 1,
        BindDepthStencil = 1 << 2
    };

}
}