#pragma once

#include "render/api/RenderResources.hpp"

#include "math/Primitivs.hpp"

namespace elm::render {

/// Update side: the offscreen color target the scene is rendered into and the UI displays.
/// Resizing goes through the resource command queue, the render thread never recreates it on its own.
class ViewPort {
public:
    ViewPort() = default;
    ViewPort(RenderResources& resources, Size size)
        : m_colorTexture(resources, MakeColorDesc(size))
        , m_size(size)
    {
    }

    ViewPort(const ViewPort&) = delete;
    ViewPort& operator=(const ViewPort&) = delete;
    ViewPort(ViewPort&&) noexcept = default;
    ViewPort& operator=(ViewPort&&) noexcept = default;

    [[nodiscard]] TextureHandle GetColorTexture() const noexcept { return m_colorTexture.GetHandle(); }
    [[nodiscard]] Size GetSize() const noexcept { return m_size; }

    void SetSize(Size size)
    {
        if (size.IsEmpty() || (size.width == m_size.width && size.height == m_size.height))
            return;
        m_size = size;
        m_colorTexture.Resize(size.width, size.height);
    }

    [[nodiscard]] bool IsValid() const noexcept { return m_colorTexture.IsValid() && !m_size.IsEmpty(); }

private:
    [[nodiscard]] static TextureDesc MakeColorDesc(Size size)
    {
        TextureDesc desc;
        desc.name = "Engine Viewport Color";
        desc.width = size.width;
        desc.height = size.height;
        desc.format = TextureFormat::RGBA8_UNORM_SRGB;
        desc.bindFlags = TextureBind::RenderTarget | TextureBind::ShaderResource;
        return desc;
    }

    Texture m_colorTexture;
    Size m_size;
};

} // namespace elm::render
