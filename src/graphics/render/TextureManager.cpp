#include "graphics/render/TextureManager.hpp"

#include "graphics/render/backends/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"

#include <cassert>

namespace elm {
namespace render {

    TextureManager* TextureManager::s_instance { nullptr };

    TextureManager& TextureManager::Get()
    {
        assert(s_instance != nullptr && "TextureManager instance is null!");
        return *s_instance;
    }

    TextureManager::TextureManager()
    {
        s_instance = this;
    }

    TextureManager::~TextureManager()
    {
        clear();
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    bool TextureManager::Init(CommandQueue* commandQueue)
    {
        m_commandQueue = commandQueue;
        return m_commandQueue != nullptr;
    }

    core::Handler TextureManager::CreateTexture(const TextureInfo& info)
    {
        core::Handler handler(++m_currentIndex, core::Handler::Render);

        m_commandQueue->Push(command::CreateTexture({ .handler = handler, .info = info }));
        return handler;
    }

    Diligent::ITexture* TextureManager::GetTextureImpl(const core::Handler& handler) const
    {
        auto it = m_textures.find(handler);
        if (it != m_textures.end()) {
            return it->second.pTexture;
        }
        return nullptr;
    }

    Diligent::ITextureView* TextureManager::GetTextureView(core::Handler handler) const
    {
        auto it = m_textures.find(handler);
        if (it != m_textures.end()) {
            return it->second.pSRV;
        }
        return nullptr;
    }

    void TextureManager::UpdateTexture(const core::Handler& handler, const TextureData& textureData)
    {

        if (textureData.data.empty())
            return;

        auto it = m_textures.find(handler);
        if (it == m_textures.end() || !it->second.pTexture)
            return;

        m_commandQueue->Push(command::UploadTexture({
            .handler = handler,
            .data = textureData,
        }));
    }

    void TextureManager::DestroyTexture(const core::Handler& handler)
    {
        auto it = m_textures.find(handler);
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

    void TextureManager::clear()
    {
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

    TextureManager::Data TextureManager::GetTextureData(const core::Handler& handler)
    {
        auto it = m_textures.find(handler);
        if (it != m_textures.end()) {
            return it->second;
        }
        return TextureManager::Data();
    }

    void TextureManager::PushTextureData(const core::Handler& handler, const Data& textureData)
    {
        m_textures[handler] = TextureManager::Data(textureData);
    }

} // namespace elm::render
}