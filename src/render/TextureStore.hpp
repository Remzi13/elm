#pragma once

#include "core/Std.hpp"
#include "core/Handler.hpp"

namespace Diligent {
	struct IRenderDevice;
	struct ITexture;
	struct TextureDesc;
}

namespace elm::render {

	class TextureStore {
	public:
		~TextureStore();

	private:
		friend class RenderResourceProvider;

		struct Data {
			Diligent::ITexture* pTexture{ nullptr };
			uint32_t width{ 0 };
			uint32_t height{ 0 };
		};

		// Render thread only. Returns the supplied handler when creation succeeds.
		[[nodiscard]] core::Handler Create(Diligent::IRenderDevice* renderDevice, core::Handler handler,
			const Diligent::TextureDesc& description);
		[[nodiscard]] const Data* Find(const core::Handler& handler) const;
			void Release(const core::Handler& handler);
		void Clear();

		void Insert(core::Handler handler, Data data);
		static void ReleaseData(Data& data);

		UnorderedMap<core::Handler, Data> m_textures;
	};

} // namespace elm::render