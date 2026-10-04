#include "graphics/render/ImGuiRenderer.hpp"

#include "graphics/ImGuiSystem.hpp"
#include "graphics/render/Texture.hpp"
#include "graphics/render/TextureManager.hpp"

#include "imgui.h"

namespace elm::render {

ImGuiRenderer::ImGuiRenderer()
{
    InitializeTextures();
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

bool ImGuiRenderer::InitializeTextures()
{
    auto& textureManager = TextureManager::Get();
    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return false;

    TextureInfo fontInfo;
    fontInfo.name = "ImGui Font Atlas";
    fontInfo.width = static_cast<uint32_t>(width);
    fontInfo.height = static_cast<uint32_t>(height);
    fontInfo.format = TextureFormat::RGBA8_UNORM;
    fontInfo.bindFlags = TextureBindFlags::BindShaderResource;
    m_fontTexture = std::make_unique<Texture>(textureManager.CreateTexture(fontInfo));
    if (!m_fontTexture->IsValid())
        return false;

    TextureData fontData;
    fontData.stride = static_cast<size_t>(width) * 4;
    fontData.data.assign(pixels, pixels + fontData.stride * static_cast<size_t>(height));
    m_fontTexture->Update(fontData);
    io.Fonts->SetTexID(ImGuiSystem::ToTextureId(m_fontTexture->GetHandler()));

    TextureInfo whiteInfo;
    whiteInfo.name = "ImGui White Texture";
    whiteInfo.width = 1;
    whiteInfo.height = 1;
    whiteInfo.format = TextureFormat::RGBA8_UNORM;
    whiteInfo.bindFlags = TextureBindFlags::BindShaderResource;
    m_whiteTexture = std::make_unique<Texture>(textureManager.CreateTexture(whiteInfo));
    if (!m_whiteTexture->IsValid())
        return false;

    TextureData whiteData;
    whiteData.stride = 4;
    whiteData.data = { 0xff, 0xff, 0xff, 0xff };
    m_whiteTexture->Update(whiteData);
    return true;
}

bool ImGuiRenderer::IsInitialized() const noexcept
{
    return m_fontTexture && m_fontTexture->IsValid() &&
        m_whiteTexture && m_whiteTexture->IsValid();
}

core::Handler ImGuiRenderer::GetFallbackTexture() const noexcept
{
    return m_whiteTexture ? m_whiteTexture->GetHandler() : core::Handler{};
}

void ImGuiRenderer::Shutdown()
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        ImGui::GetIO().Fonts->SetTexID(nullptr);
    m_fontTexture.reset();
    m_whiteTexture.reset();
}

} // namespace elm::render
