#include "render/ResourceView.hpp"

#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"

#include <utility>

namespace elm::render {

ResourceView::ResourceView(Diligent::ITexture* texture, ResourceViewType type) noexcept
    : m_type(type)
{
    if (!texture)
        return;

    Diligent::ITextureView* view = nullptr;
    switch (type) {
    case ResourceViewType::ShaderResource:
        view = texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
        break;
    case ResourceViewType::RenderTarget:
        view = texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        break;
    case ResourceViewType::DepthStencil:
        view = texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        break;
    default:
        return;
    }

    if (!view)
        return;

    texture->AddRef();
    view->AddRef();
    m_texture = texture;
    m_view = view;
}

ResourceView::~ResourceView()
{
    Reset();
}

ResourceView::ResourceView(ResourceView&& other) noexcept
    : m_texture(std::exchange(other.m_texture, nullptr))
    , m_view(std::exchange(other.m_view, nullptr))
    , m_type(other.m_type)
{
}

ResourceView& ResourceView::operator=(ResourceView&& other) noexcept
{
    if (this != &other) {
        Reset();
        m_texture = std::exchange(other.m_texture, nullptr);
        m_view = std::exchange(other.m_view, nullptr);
        m_type = other.m_type;
    }
    return *this;
}

void ResourceView::Reset() noexcept
{
    if (m_view) {
        m_view->Release();
        m_view = nullptr;
    }
    if (m_texture) {
        m_texture->Release();
        m_texture = nullptr;
    }
}

} // namespace elm::render