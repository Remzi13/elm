#pragma once

#include "core/Std.hpp"

#include "math/Primitivs.hpp"

#include <cstdint>

namespace elm::render {

    enum class TextureFormat : uint8_t {
        Unknown,
        RGBA8_UNORM,
        RGBA8_UNORM_SRGB,
        BGRA8_UNORM,
        BGRA8_UNORM_SRGB,
        R32_FLOAT,
        D32_FLOAT,
        D24_UNORM_S8_UINT,
    };

    [[nodiscard]] constexpr bool isDepthFormat(TextureFormat format) noexcept
    {
        return format == TextureFormat::D32_FLOAT || format == TextureFormat::D24_UNORM_S8_UINT;
    }

    [[nodiscard]] constexpr bool isSrgbFormat(TextureFormat format) noexcept
    {
        return format == TextureFormat::RGBA8_UNORM_SRGB || format == TextureFormat::BGRA8_UNORM_SRGB;
    }

    enum class ResourceUsage : uint8_t {
        Default,   // GPU memory, updated with upload commands
        Immutable, // initialized once at creation
        Dynamic,   // rewritten every frame
    };

    namespace TextureBind {
        enum : uint32_t {
            ShaderResource = 1 << 0,
            RenderTarget = 1 << 1,
            DepthStencil = 1 << 2,
        };
    }

    struct TextureDesc {
        String name;
        uint32_t width { 0 };
        uint32_t height { 0 };
        TextureFormat format { TextureFormat::RGBA8_UNORM };
        ResourceUsage usage { ResourceUsage::Default };
        uint32_t bindFlags { TextureBind::ShaderResource };
    };

    /// Pixels for the top mip level. stride == 0 means tightly packed rows.
    struct TextureData {
        Vector<uint8_t> data;
        size_t stride { 0 };
    };

    enum class BufferType : uint8_t {
        Vertex,
        Index,
        Uniform,
    };

    struct BufferDesc {
        String name;
        BufferType type { BufferType::Vertex };
        uint64_t size { 0 };
        ResourceUsage usage { ResourceUsage::Default };
    };

    /// Mesh geometry is shared with the render thread, the update side must not change it afterwards.
    using MeshDataPtr = SharedPtr<const MeshData>;

} // namespace elm::render
