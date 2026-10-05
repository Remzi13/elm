#include "graphics/ui/ImGuiRenderer.hpp"

#include "imgui.h"

namespace elm::render {

    namespace {
        ImTextureID ToTextureId(render::TextureHandle texture)
        {
            if (!texture.IsValid())
                return nullptr;
            // Real texture views are aligned pointers, so the lowest bit marks an engine handle
            const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
            return reinterpret_cast<ImTextureID>(value);
        }
    }

ImGuiRenderer::ImGuiRenderer(RenderResources& resources)
{
    InitializeTextures(resources);
}

ImGuiRenderer::~ImGuiRenderer()
{
    Shutdown();
}

bool ImGuiRenderer::InitializeTextures(RenderResources& resources)
{
    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return false;

    TextureDesc fontInfo;
    fontInfo.name = "ImGui Font Atlas";
    fontInfo.width = static_cast<uint32_t>(width);
    fontInfo.height = static_cast<uint32_t>(height);
    fontInfo.format = TextureFormat::RGBA8_UNORM;
    fontInfo.bindFlags = TextureBind::ShaderResource;
    m_fontTexture = Texture(resources, fontInfo);

    TextureData fontData;
    fontData.stride = static_cast<size_t>(width) * 4;
    fontData.data.assign(pixels, pixels + fontData.stride * static_cast<size_t>(height));
    m_fontTexture.Update(std::move(fontData));
    io.Fonts->SetTexID(ToTextureId(m_fontTexture.GetHandle()));

    TextureDesc whiteInfo;
    whiteInfo.name = "ImGui White Texture";
    whiteInfo.width = 1;
    whiteInfo.height = 1;
    whiteInfo.format = TextureFormat::RGBA8_UNORM;
    whiteInfo.bindFlags = TextureBind::ShaderResource;
    m_whiteTexture = Texture(resources, whiteInfo);

    TextureData whiteData;
    whiteData.stride = 4;
    whiteData.data = { 0xff, 0xff, 0xff, 0xff };
    m_whiteTexture.Update(std::move(whiteData));
    return true;
}

bool ImGuiRenderer::IsInitialized() const noexcept
{
    return m_fontTexture.IsValid() && m_whiteTexture.IsValid();
}

TextureHandle ImGuiRenderer::GetFallbackTexture() const noexcept
{
    return m_whiteTexture.GetHandle();
}

void ImGuiRenderer::Shutdown()
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        ImGui::GetIO().Fonts->SetTexID(nullptr);
    m_fontTexture.Reset();
    m_whiteTexture.Reset();
}

} // namespace elm::render
