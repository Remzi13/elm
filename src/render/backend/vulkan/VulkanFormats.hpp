#pragma once

#include "render/api/PipelineDesc.hpp"
#include "render/api/ResourceDesc.hpp"

#include <vulkan/vulkan.h>

namespace elm::render::vulkan {

    [[nodiscard]] VkFormat ToVkFormat(TextureFormat format) noexcept;
    [[nodiscard]] TextureFormat FromVkFormat(VkFormat format) noexcept;

    [[nodiscard]] VkFormat ToVkFormat(VertexFormat format) noexcept;
    [[nodiscard]] uint32_t VertexFormatSize(VertexFormat format) noexcept;

    [[nodiscard]] VkImageUsageFlags ToImageUsage(uint32_t textureBindFlags) noexcept;
    /// Depth for depth formats (stencil is never sampled or cleared), color otherwise.
    [[nodiscard]] VkImageAspectFlags ImageAspect(TextureFormat format) noexcept;

    [[nodiscard]] VkShaderStageFlags ToShaderStage(ShaderStage stage) noexcept;

} // namespace elm::render::vulkan
