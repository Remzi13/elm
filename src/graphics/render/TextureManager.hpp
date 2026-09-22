#pragma once

#include "core/Std.hpp"
#include "core/Handler.hpp"

#include "graphics/render/Texture.hpp"
#include "graphics/render/CommandQueue.hpp"

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

		bool Init(CommandQueue* commandQueue);

		[[nodiscard]] core::Handler CreateTexture(const TextureInfo& info);	

		[[nodiscard]] Diligent::ITexture* GetTextureImpl(const core::Handler& handler) const;		
		[[nodiscard]] Diligent::ITextureView* GetTextureView(core::Handler handler) const;		

		void UpdateTexture(const core::Handler& handler, const TextureData& textureData);
		void DestroyTexture(const core::Handler& handler);

		Data GetTextureData(const core::Handler& handler);
		void PushTextureData(const core::Handler& handler, const Data& textureData);
		
		void clear();

	private:
		static TextureManager* s_instance;
		UnorderedMap<core::Handler, Data> m_textures;
		int m_currentIndex{ 0 };

		CommandQueue* m_commandQueue{ nullptr };
	};

} // namespace elm::render
