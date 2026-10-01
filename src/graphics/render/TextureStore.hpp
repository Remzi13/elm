#pragma once

#include "core/Std.hpp"
#include "core/Handler.hpp"

namespace Diligent {
	struct ITexture;
	struct ITextureView;
}

namespace elm::render {

	class TextureStore {
	public:
		struct Data {
			Diligent::ITexture* pTexture{ nullptr };
			Diligent::ITextureView* pSRV{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		~TextureStore();

		// Render thread only. Insert adopts the supplied references; Register adds its own references.
		void Insert(core::Handler handler, Data data);
		void Register(core::Handler handler, const Data& data);
		[[nodiscard]] const Data* Find(const core::Handler& handler) const;
		[[nodiscard]] Diligent::ITextureView* GetTextureView(const core::Handler& handler) const;
		void Release(const core::Handler& handler);
		void Clear();

	private:
		UnorderedMap<core::Handler, Data> m_textures;
	};

} // namespace elm::render