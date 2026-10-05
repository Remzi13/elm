#pragma once

#include "core/Std.hpp"

#include "render/api/Handles.hpp"
#include "render/api/ResourceDesc.hpp"

#include <cstdint>

namespace elm::render {

    enum class ShaderStage : uint8_t {
        Vertex,
        Pixel,
    };

    struct ShaderMacro {
        String name;
        String value;
    };

    /// HLSL source loaded from the shaders/ directory, entry point "main".
    struct ShaderDesc {
        String name;
        ShaderStage stage { ShaderStage::Vertex };
        String sourceFile;
        Vector<ShaderMacro> macros;
    };

    enum class VertexFormat : uint8_t {
        Float2,
        Float3,
        Float4,
    };

    struct VertexAttribute {
        uint32_t location { 0 };
        uint32_t bufferSlot { 0 };
        VertexFormat format { VertexFormat::Float3 };
        bool perInstance { false };
    };

    enum class BlendMode : uint8_t {
        Opaque,
        /// src * 1 + dst * (1 - src.a)
        PremultipliedAlpha,
        /// src * src.a + dst * (1 - src.a)
        AlphaBlend,
    };

    enum class CullMode : uint8_t {
        None,
        Back,
        Front,
    };

    enum class BindingType : uint8_t {
        /// Uniform buffer filled per draw/pass through CommandList::SetConstants.
        Constants,
        /// Texture bound through CommandList::BindTexture.
        Texture,
    };

    /// A shader resource of the pipeline. Its index in PipelineDesc::bindings is the slot used by commands.
    struct BindingDesc {
        String name;
        ShaderStage stage { ShaderStage::Vertex };
        BindingType type { BindingType::Constants };
        /// Constants only: buffer size in bytes.
        uint32_t size { 0 };
    };

    /// Linear clamp sampler bound to "<texture>_sampler".
    struct SamplerDesc {
        String textureName;
        ShaderStage stage { ShaderStage::Pixel };
    };

    struct PipelineDesc {
        String name;
        ShaderHandle vertexShader;
        ShaderHandle pixelShader;
        Vector<VertexAttribute> vertexLayout;
        TextureFormat colorFormat { TextureFormat::Unknown };
        TextureFormat depthFormat { TextureFormat::Unknown };
        BlendMode blend { BlendMode::Opaque };
        CullMode cull { CullMode::None };
        bool depthTest { false };
        bool depthWrite { false };
        bool scissor { false };
        Vector<BindingDesc> bindings;
        Vector<SamplerDesc> samplers;
    };

} // namespace elm::render
