#include "render/TextureManager.hpp"

namespace elm {
namespace render {

    TextureManager& TextureManager::Get()
    {
        static TextureManager instance;
        return instance;
    }

    TextureManager::TextureManager() = default;

    TextureManager::~TextureManager()
    {
        Shutdown();
    }

    bool TextureManager::Init(CommandQueue* commandQueue)
    {
        m_commandQueue = commandQueue;
        return m_commandQueue != nullptr;
    }

    void TextureManager::Shutdown()
    {
        m_commandQueue = nullptr;
        std::lock_guard<std::mutex> lock(m_textureInfosMutex);
        m_textureInfos.clear();
    }

    Texture TextureManager::CreateTexture(const TextureInfo& info)
    {
        if (!m_commandQueue) {
            return {};
        }

        const core::Handler handler(++m_currentIndex, core::Handler::Render);
        {
            std::lock_guard<std::mutex> lock(m_textureInfosMutex);
            m_textureInfos.emplace(handler, info);
        }
        m_commandQueue->Push(command::resource::CreateTexture({ .handler = handler, .info = info }));
        return Texture(info, handler);
    }

    void TextureManager::UpdateTexture(const core::Handler& handler, const TextureData& textureData)
    {
        if (!m_commandQueue || textureData.data.empty() || !handler.IsValid())
            return;

        // Texture existence is checked by the executor on the render thread
        m_commandQueue->Push(command::resource::UploadTexture({
            .handler = handler,
            .data = textureData,
        }));
    }

    std::optional<TextureInfo> TextureManager::GetTextureInfo(const core::Handler& handler) const
    {
        std::lock_guard<std::mutex> lock(m_textureInfosMutex);
        const auto it = m_textureInfos.find(handler);
        if (it == m_textureInfos.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void TextureManager::UnregisterTexture(const core::Handler& handler)
    {
        std::lock_guard<std::mutex> lock(m_textureInfosMutex);
        m_textureInfos.erase(handler);
    }

    void TextureManager::DestroyTexture(const core::Handler& handler)
    {
        if (!handler.IsValid())
            return;

        UnregisterTexture(handler);
        if (!m_commandQueue)
            return;

        // The render thread may still draw with this texture, so it is released there
        m_commandQueue->Push(command::resource::DestroyTexture { handler });
    }

} // namespace elm::render
}