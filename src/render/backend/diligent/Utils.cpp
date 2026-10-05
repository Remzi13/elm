#include "render/backend/diligent/Utils.hpp"

namespace elm::render {

	Diligent::BIND_FLAGS getBindFlags(BufferType type) {
		switch (type) {
		case BufferType::Vertex:
			return Diligent::BIND_VERTEX_BUFFER;
		case BufferType::Index:
			return Diligent::BIND_INDEX_BUFFER;
		case BufferType::Uniform:
			return Diligent::BIND_UNIFORM_BUFFER;
		default:
			return Diligent::BIND_NONE;
		}
	}

	Diligent::TEXTURE_FORMAT getTextureFormat(TextureFormat format) {
		switch (format) {
		case TextureFormat::RGBA8_UNORM:
			return Diligent::TEX_FORMAT_RGBA8_UNORM;
		case TextureFormat::RGBA8_UNORM_SRGB:
			return Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
		case TextureFormat::BGRA8_UNORM:
			return Diligent::TEX_FORMAT_BGRA8_UNORM;
		case TextureFormat::BGRA8_UNORM_SRGB:
			return Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB;
		case TextureFormat::R32_FLOAT:
			return Diligent::TEX_FORMAT_R32_FLOAT;
		case TextureFormat::D32_FLOAT:
			return Diligent::TEX_FORMAT_D32_FLOAT;
		case TextureFormat::D24_UNORM_S8_UINT:
			return Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
		case TextureFormat::Unknown:
		default:
			return Diligent::TEX_FORMAT_UNKNOWN;
		}
	}

	TextureFormat getEngineTextureFormat(Diligent::TEXTURE_FORMAT format) {
		switch (format) {
		case Diligent::TEX_FORMAT_RGBA8_UNORM:
			return TextureFormat::RGBA8_UNORM;
		case Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB:
			return TextureFormat::RGBA8_UNORM_SRGB;
		case Diligent::TEX_FORMAT_BGRA8_UNORM:
			return TextureFormat::BGRA8_UNORM;
		case Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB:
			return TextureFormat::BGRA8_UNORM_SRGB;
		case Diligent::TEX_FORMAT_R32_FLOAT:
			return TextureFormat::R32_FLOAT;
		case Diligent::TEX_FORMAT_D32_FLOAT:
			return TextureFormat::D32_FLOAT;
		case Diligent::TEX_FORMAT_D24_UNORM_S8_UINT:
			return TextureFormat::D24_UNORM_S8_UINT;
		default:
			return TextureFormat::Unknown;
		}
	}

	Diligent::USAGE getUsage(ResourceUsage usage) {
		switch (usage) {
		case ResourceUsage::Immutable:
			return Diligent::USAGE_IMMUTABLE;
		case ResourceUsage::Dynamic:
			return Diligent::USAGE_DYNAMIC;
		case ResourceUsage::Default:
		default:
			return Diligent::USAGE_DEFAULT;
		}
	}

	Diligent::BIND_FLAGS getTextureBindFlags(uint32_t bindFlags) {
		Diligent::BIND_FLAGS flags = Diligent::BIND_NONE;
		if (bindFlags & TextureBind::ShaderResource) {
			flags |= Diligent::BIND_SHADER_RESOURCE;
		}
		if (bindFlags & TextureBind::RenderTarget) {
			flags |= Diligent::BIND_RENDER_TARGET;
		}
		if (bindFlags & TextureBind::DepthStencil) {
			flags |= Diligent::BIND_DEPTH_STENCIL;
		}
		return flags;
	}

}
