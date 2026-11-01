#pragma once

#include "core/Std.hpp"

#include "render/api/PipelineDesc.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace elm::render::vulkan {

    /// One shader stage as SPIR-V, compiled from the HLSL source at build time (cmake/Shaders.cmake).
    struct SpirvShader {
        String name;
        ShaderStage stage { ShaderStage::Vertex };
        Vector<uint32_t> words;
    };

    /// Binding a resource variable of set 0 gets, by its HLSL name.
    using SpirvBindings = UnorderedMap<String, uint32_t>;

    /// "spirv/overlay.frag.OVERLAY_MANUAL_SRGB_1.spv" for overlay.frag.hlsl with OVERLAY_MANUAL_SRGB=1.
    [[nodiscard]] String SpirvFileName(const ShaderDesc& desc);

    /// Reads the compiled shader from the shaders directory; nullopt (logged) when missing or malformed.
    [[nodiscard]] std::optional<SpirvShader> LoadSpirvShader(const ShaderDesc& desc);

    /// Copy of the module where every variable named in bindings is decorated with that binding.
    /// DXC numbers each stage from 0, while the pipeline layout is shared by both stages.
    [[nodiscard]] Vector<uint32_t> RemapBindings(std::span<const uint32_t> words, const SpirvBindings& bindings);

} // namespace elm::render::vulkan
