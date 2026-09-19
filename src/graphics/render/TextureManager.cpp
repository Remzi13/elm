#include "graphics/render/TextureManager.hpp"
#include "graphics/render/backends/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"

#include <cassert>

namespace elm::render {

	TextureManager* TextureManager::s_instance{ nullptr };

	TextureManager& TextureManager::Get() {
		assert(s_instance != nullptr && "TextureManager instance is null!");
		return *s_instance;
	}

	TextureManager::TextureManager() {
		s_instance = this;
	}

	TextureManager::~TextureManager() {
		clear();
		if (s_instance == this) {
			s_instance = nullptr;
		}
	}

	bool TextureManager::Init(CommandList* commandList) {
		m_commandList = commandList;
		return m_commandList != nullptr;
	}

	Handler TextureManager::CreateTexture(const TextureInfo& info) {

		Handler handler;
		handler.index = ++m_currentIndex;

		m_commandList->Push(command::CreateTexture({ .handler = handler, .info=info}));
		return handler;
	}

	Diligent::ITexture* TextureManager::GetTextureImpl(const Handler& handler) const {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			return it->second.pTexture;
		}
		return nullptr;
	}

	Diligent::ITextureView* TextureManager::GetTextureView(Handler handler) const {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			return it->second.pSRV;
		}
		return nullptr;
	}

	void TextureManager::UpdateTexture(const Handler& handler, const TextureData& textureData) {

		if (textureData.data.empty()) return;

		auto it = m_textures.find(handler.index);
		if (it == m_textures.end() || !it->second.pTexture) return;

		m_commandList->Push(command::UploadTexture({ .handler = handler, .data=textureData,}));
	}

	void TextureManager::DestroyTexture(const Handler& handler) {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			if (it->second.pSRV) {
				it->second.pSRV->Release();
				it->second.pSRV = nullptr;
			}
			if (it->second.pTexture) {
				it->second.pTexture->Release();
				it->second.pTexture = nullptr;
			}
			m_textures.erase(it);
		}
	}

	void TextureManager::clear() {
		for (auto& [_, texData] : m_textures) {
			if (texData.pSRV) {
				texData.pSRV->Release();
				texData.pSRV = nullptr;
			}
			if (texData.pTexture) {
				texData.pTexture->Release();
				texData.pTexture = nullptr;
			}
		}
		m_textures.clear();
	}

	TextureManager::Data TextureManager::GetTextureData(const Handler& handler) {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			return it->second;
		}
		return TextureManager::Data();
	}

	void TextureManager::PushTextureData(const Handler& handler, const Data& textureData)
	{
		m_textures[handler.index] = TextureManager::Data(textureData);
	}

} // namespace elm::render

