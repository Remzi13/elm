#pragma once 

namespace elm::render {

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
		BindRenderTarget   = 1 << 1,
		BindDepthStencil   = 1 << 2
	};

}