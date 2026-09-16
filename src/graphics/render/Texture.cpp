#include "graphics/render/Texture.hpp"
#include "graphics/render/TextureManager.hpp"

namespace elm::render {

	Texture::Texture(const TextureInfo& info)
		: m_info(info) {
		m_handler = TextureManager::Get().CreateTexture(info);
	}

	Texture::~Texture() {
		if (m_handler.IsValid()) {
			TextureManager::Get().DestroyTexture(m_handler);
		}
	}

	Texture::Texture(Texture&& other) noexcept
		: m_handler(std::exchange(other.m_handler, {})), m_info(std::move(other.m_info)) {
	}

	Texture& Texture::operator=(Texture&& other) noexcept {
		if (this != &other) {
			if (m_handler.IsValid()) {
				TextureManager::Get().DestroyTexture(m_handler);
			}
			m_handler = std::exchange(other.m_handler, {});
			m_info = std::move(other.m_info);
		}
		return *this;
	}

	void Texture::Update(const void* data, size_t stride) {
		TextureManager::Get().UpdateTexture(m_handler, data, stride);
	}

} // namespace elm::render
