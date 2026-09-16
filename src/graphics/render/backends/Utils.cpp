#include "graphics/render/backends/Utils.hpp"

namespace elm::render {

	Diligent::BIND_FLAGS getBindFlags(BufferType type) {
		switch (type) {
		case BufferType::VertexBuffer:
			return Diligent::BIND_VERTEX_BUFFER;
		case BufferType::IndexBuffer:
			return Diligent::BIND_INDEX_BUFFER;
		case BufferType::UniformBuffer:
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
		case TextureFormat::R32_FLOAT:
			return Diligent::TEX_FORMAT_R32_FLOAT;
		case TextureFormat::D32_FLOAT:
			return Diligent::TEX_FORMAT_D32_FLOAT;
		default:
			return Diligent::TEX_FORMAT_UNKNOWN;
		}
	}

	Diligent::USAGE getUsage(TextureUsage usage) {
		switch (usage) {
		case TextureUsage::Immutable:
			return Diligent::USAGE_IMMUTABLE;
		case TextureUsage::Dynamic:
			return Diligent::USAGE_DYNAMIC;
		case TextureUsage::Default:
		default:
			return Diligent::USAGE_DEFAULT;
		}
	}

	Diligent::BIND_FLAGS getTextureBindFlags(uint32_t bindFlags) {
		Diligent::BIND_FLAGS flags = Diligent::BIND_NONE;
		if (bindFlags & TextureBindFlags::BindShaderResource) {
			flags |= Diligent::BIND_SHADER_RESOURCE;
		}
		if (bindFlags & TextureBindFlags::BindRenderTarget) {
			flags |= Diligent::BIND_RENDER_TARGET;
		}
		if (bindFlags & TextureBindFlags::BindDepthStencil) {
			flags |= Diligent::BIND_DEPTH_STENCIL;
		}
		return flags;
	}

}