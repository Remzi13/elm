#pragma once

#include "core/Std.hpp"
#include "graphics/render/Render.hpp"
#include "graphics/render/Texture.hpp"

namespace Diligent {
	struct IRenderDevice;
	struct IDeviceContext;
	class ITexture;
	class ITextureView;
}

namespace elm::render {

	class TextureManager {
	public:
		TextureManager();
		~TextureManager();

		bool Init(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext = nullptr);
		void SetDeviceContext(Diligent::IDeviceContext* deviceContext) { m_deviceContext = deviceContext; }

		[[nodiscard]] Texture createTexture(const TextureInfo& info);
		[[nodiscard]] Diligent::ITexture* getTextureImpl(const TextureHandler& handler) const;
		[[nodiscard]] Diligent::ITexture* getTextureImpl(const Texture& texture) const { return getTextureImpl(texture.GetHandler()); }
		[[nodiscard]] Diligent::ITextureView* getTextureSRV(const TextureHandler& handler) const;
		[[nodiscard]] Diligent::ITextureView* getTextureSRV(const Texture& texture) const { return getTextureSRV(texture.GetHandler()); }

		void updateTexture(const TextureHandler& handler, const void* data, size_t stride = 0);
		void updateTexture(const Texture& texture, const void* data, size_t stride = 0) {
			updateTexture(texture.GetHandler(), data, stride);
		}
		void updateTexture(Diligent::IDeviceContext* deviceContext, const TextureHandler& handler, const void* data, size_t stride = 0);
		void updateTexture(Diligent::IDeviceContext* deviceContext, const Texture& texture, const void* data, size_t stride = 0) {
			updateTexture(deviceContext, texture.GetHandler(), data, stride);
		}

		void destroyTexture(const TextureHandler& handler);
		void destroyTexture(const Texture& texture) { destroyTexture(texture.GetHandler()); }

		void clear();

	private:
		struct TextureData {
			Diligent::ITexture* pTexture{ nullptr };
			Diligent::ITextureView* pSRV{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		Diligent::IRenderDevice* m_renderDevice{ nullptr };
		Diligent::IDeviceContext* m_deviceContext{ nullptr };
		UnorderedMap<int, TextureData> m_textures;
		int m_currentIndex{ 0 };
	};

} // namespace elm::render
