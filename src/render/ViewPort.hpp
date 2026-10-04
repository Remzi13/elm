#pragma once

#include "render/Texture.hpp"
#include "math/Primitivs.hpp"

#include <utility>

namespace elm::render {

/// Owns the engine viewport render targets and their current dimensions.
class ViewPort {
public:
    struct Snapshot {
        core::Handler colorTexture;
        core::Handler depthTexture;
        Size size;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return colorTexture.IsValid() && depthTexture.IsValid() && !size.IsEmpty();
        }
    };

    ViewPort() = default;
    ViewPort(Texture colorTexture, Texture depthTexture, Size size) noexcept
        : m_colorTexture(std::move(colorTexture))
        , m_depthTexture(std::move(depthTexture))
        , m_size(size)
    {
    }

    ViewPort(const ViewPort&) = delete;
    ViewPort& operator=(const ViewPort&) = delete;
    ViewPort(ViewPort&&) noexcept = default;
    ViewPort& operator=(ViewPort&&) noexcept = default;

    [[nodiscard]] const Texture& GetColorTexture() const noexcept { return m_colorTexture; }
    [[nodiscard]] const Texture& GetDepthTexture() const noexcept { return m_depthTexture; }
    [[nodiscard]] Size GetSize() const noexcept { return m_size; }

    void SetSize(Size size) noexcept { m_size = size; }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_colorTexture.IsValid() && m_depthTexture.IsValid() && !m_size.IsEmpty();
    }

    [[nodiscard]] Snapshot GetSnapshot() const noexcept
    {
        return { m_colorTexture.GetHandler(), m_depthTexture.GetHandler(), m_size };
    }

private:
    Texture m_colorTexture;
    Texture m_depthTexture;
    Size m_size;
};

} // namespace elm::render
