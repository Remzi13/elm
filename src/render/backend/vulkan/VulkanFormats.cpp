#include "render/backend/vulkan/VulkanFormats.hpp"

namespace elm::render::vulkan {

    VkFormat ToVkFormat(TextureFormat format) noexcept
    {
        switch (format) {
        case TextureFormat::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::BGRA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8_UNORM_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::R32_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::D32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D24_UNORM_S8_UINT:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Unknown:
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    TextureFormat FromVkFormat(VkFormat format) noexcept
    {
        switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return TextureFormat::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return TextureFormat::RGBA8_UNORM_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return TextureFormat::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return TextureFormat::BGRA8_UNORM_SRGB;
        case VK_FORMAT_R32_SFLOAT:
            return TextureFormat::R32_FLOAT;
        case VK_FORMAT_D32_SFLOAT:
            return TextureFormat::D32_FLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return TextureFormat::D24_UNORM_S8_UINT;
        default:
            return TextureFormat::Unknown;
        }
    }

    VkFormat ToVkFormat(VertexFormat format) noexcept
    {
        switch (format) {
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UNorm8x4:
            return VK_FORMAT_R8G8B8A8_UNORM;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    uint32_t VertexFormatSize(VertexFormat format) noexcept
    {
        switch (format) {
        case VertexFormat::Float2:
            return 8;
        case VertexFormat::Float3:
            return 12;
        case VertexFormat::Float4:
            return 16;
        case VertexFormat::UNorm8x4:
            return 4;
        default:
            return 0;
        }
    }

    VkImageUsageFlags ToImageUsage(uint32_t textureBindFlags) noexcept
    {
        // Every texture can be filled by UploadTexture
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (textureBindFlags & TextureBind::ShaderResource)
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (textureBindFlags & TextureBind::RenderTarget)
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (textureBindFlags & TextureBind::DepthStencil)
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        return usage;
    }

    VkImageAspectFlags ImageAspect(TextureFormat format) noexcept
    {
        return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkShaderStageFlags ToShaderStage(ShaderStage stage) noexcept
    {
        return stage == ShaderStage::Pixel ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    }

} // namespace elm::render::vulkan
