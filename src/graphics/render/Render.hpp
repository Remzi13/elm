#pragma once

namespace elm {
namespace render {

    struct Handler {
        int index { -1 };

        [[nodiscard]] constexpr bool IsValid() const noexcept { return index >= 0; }
        constexpr auto operator<=>(const Handler&) const noexcept = default;
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