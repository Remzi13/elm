#include "graphics/render/Texture.hpp"
#include "graphics/render/TextureManager.hpp"

namespace elm::render {

	Texture::Texture(TextureHandler handler, const TextureInfo& info, TextureManager* manager)
		: m_handler(handler), m_info(info), m_manager(manager) {
	}

	void Texture::Update(const void* data, size_t stride) {
		if (m_manager && m_handler.IsValid()) {
			m_manager->updateTexture(m_handler, data, stride > 0 ? stride : (m_info.width * sizeof(uint32_t)));
		}
	}

} // namespace elm::render
