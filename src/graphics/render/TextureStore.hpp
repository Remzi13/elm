#pragma once

#include "core/Std.hpp"
#include "core/Handler.hpp"

namespace Diligent {
	class IRenderDevice;
	struct ITexture;
	struct ITextureView;
	struct TextureDesc;
}

namespace elm::render {

	class TextureStore {
	public:
		struct Data {
			Diligent::ITexture* pTexture{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		~TextureStore();

		// Render thread only. Insert adopts the supplied references; Register adds its own references.
		// Render thread only. Returns the supplied handler when creation succeeds.
		[[nodiscard]] core::Handler Create(Diligent::IRenderDevice* renderDevice, core::Handler handler,
			const Diligent::TextureDesc& description);
		[[nodiscard]] const Data* Find(const core::Handler& handler) const;
		[[nodiscard]] Diligent::ITextureView* GetTextureView(const core::Handler& handler) const;
		void Release(const core::Handler& handler);
		void Clear();

	private:
		void Insert(core::Handler handler, Data data);

		UnorderedMap<core::Handler, Data> m_textures;
	};

} // namespace elm::render