#include "graphics/render/TextureManager.hpp"
#include "graphics/render/backends/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
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

	bool TextureManager::Init(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext) {
		m_renderDevice = renderDevice;
		m_deviceContext = deviceContext;
		return m_renderDevice != nullptr;
	}

	TextureHandler TextureManager::CreateTexture(const TextureInfo& info) {
		if (!m_renderDevice) {
			return TextureHandler{};
		}

		Diligent::TextureDesc TexDesc;
		TexDesc.Name = info.name.c_str();
		TexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
		TexDesc.Width = info.width;
		TexDesc.Height = info.height;
		TexDesc.Format = getTextureFormat(info.format);
		TexDesc.Usage = getUsage(info.usage);
		TexDesc.BindFlags = getTextureBindFlags(info.bindFlags);

		Diligent::TextureData InitData;
		Diligent::TextureSubResData Level0Data;
		if (info.data != nullptr) {
			Level0Data.pData = info.data;
			Level0Data.Stride = info.stride > 0 ? info.stride : info.width * sizeof(uint32_t);
			InitData.pSubResources = &Level0Data;
			InitData.NumSubresources = 1;
		}

		Diligent::ITexture* pTexture{ nullptr };
		m_renderDevice->CreateTexture(TexDesc, info.data != nullptr ? &InitData : nullptr, &pTexture);
		if (!pTexture) {
			return TextureHandler{};
		}

		Diligent::ITextureView* pSRV{ nullptr };
		if (TexDesc.BindFlags & Diligent::BIND_SHADER_RESOURCE) {
			pSRV = pTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
			if (pSRV) {
				pSRV->AddRef();
			}
		}

		TextureHandler handler;
		handler.index = ++m_currentIndex;

		TextureData texData;
		texData.pTexture = pTexture;
		texData.pSRV = pSRV;
		texData.width = info.width;
		texData.height = info.height;

		m_textures.emplace(handler.index, texData);
		return handler;
	}

	Diligent::ITexture* TextureManager::GetTextureImpl(const TextureHandler& handler) const {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			return it->second.pTexture;
		}
		return nullptr;
	}

	Diligent::ITextureView* TextureManager::GetTextureView(TextureHandler handler) const {
		auto it = m_textures.find(handler.index);
		if (it != m_textures.end()) {
			return it->second.pSRV;
		}
		return nullptr;
	}

	void TextureManager::UpdateTexture(const TextureHandler& handler, const void* data, size_t stride) {
		UpdateTexture(m_deviceContext, handler, data, stride);
	}

	void TextureManager::UpdateTexture(Diligent::IDeviceContext* deviceContext, const TextureHandler& handler, const void* data, size_t stride) {
		if (!deviceContext || !data) return;

		auto it = m_textures.find(handler.index);
		if (it == m_textures.end() || !it->second.pTexture) return;

		const auto& texData = it->second;

		Diligent::Box UpdateBox;
		UpdateBox.MinX = 0;
		UpdateBox.MaxX = texData.width;
		UpdateBox.MinY = 0;
		UpdateBox.MaxY = texData.height;

		Diligent::TextureSubResData SubresData;
		SubresData.Stride = stride > 0 ? stride : (texData.width * sizeof(uint32_t));
		SubresData.pData = data;

		deviceContext->UpdateTexture(texData.pTexture, 0, 0, UpdateBox, SubresData,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	void TextureManager::DestroyTexture(const TextureHandler& handler) {
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

} // namespace elm::render

