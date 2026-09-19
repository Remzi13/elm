#pragma once

#include "core/Std.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/Command.hpp"

namespace Diligent {
	struct ITexture;
	struct ITextureView;
}

namespace elm::render {

	class TextureManager {
	public:
		struct Data {
			Diligent::ITexture* pTexture{ nullptr };
			Diligent::ITextureView* pSRV{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};
	public:
		TextureManager();
		~TextureManager();

		static TextureManager& Get();

		bool Init(CommandList* commandList);

		[[nodiscard]] Handler CreateTexture(const TextureInfo& info);	

		[[nodiscard]] Diligent::ITexture* GetTextureImpl(const Handler& handler) const;		
		[[nodiscard]] Diligent::ITextureView* GetTextureView(Handler handler) const;		

		void UpdateTexture(const Handler& handler, const TextureData& textureData);
		void DestroyTexture(const Handler& handler);

		Data GetTextureData(const Handler& handler);
		void PushTextureData(const Handler& handler, const Data& textureData);
		
		void clear();

	private:
		static TextureManager* s_instance;
		UnorderedMap<int, Data> m_textures;
		int m_currentIndex{ 0 };

		CommandList* m_commandList{ nullptr };
	};

} // namespace elm::render
