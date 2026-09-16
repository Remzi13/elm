#pragma once

#include "core/Std.hpp"
#include "graphics/render/Texture.hpp"

namespace Diligent {
	struct IRenderDevice;
	struct IDeviceContext;
	class ITexture;
	struct ITextureView;
}

namespace elm::render {

	class TextureManager {
	public:
		TextureManager();
		~TextureManager();

		static TextureManager& Get();

		bool Init(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext = nullptr);
		void SetDeviceContext(Diligent::IDeviceContext* deviceContext) { m_deviceContext = deviceContext; }

		[[nodiscard]] TextureHandler CreateTexture(const TextureInfo& info);	

		[[nodiscard]] Diligent::ITexture* GetTextureImpl(const TextureHandler& handler) const;		
		[[nodiscard]] Diligent::ITextureView* GetTextureView(TextureHandler handler) const;		

		void UpdateTexture(const TextureHandler& handler, const void* data, size_t stride = 0);		
		void UpdateTexture(Diligent::IDeviceContext* deviceContext, const TextureHandler& handler, const void* data, size_t stride = 0);			
		void DestroyTexture(const TextureHandler& handler);
		
		void clear();

	private:
		struct TextureData {
			Diligent::ITexture* pTexture{ nullptr };
			Diligent::ITextureView* pSRV{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		static TextureManager* s_instance;

		Diligent::IRenderDevice* m_renderDevice{ nullptr };
		Diligent::IDeviceContext* m_deviceContext{ nullptr };
		UnorderedMap<int, TextureData> m_textures;
		int m_currentIndex{ 0 };
	};

} // namespace elm::render
