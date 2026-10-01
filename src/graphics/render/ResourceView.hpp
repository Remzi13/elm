#pragma once

#include "core/Handler.hpp"
#include <cstdint>

namespace Diligent {
struct ITexture;
class ITextureView;
}

namespace elm::render {

class TextureStore;

enum class ResourceViewType : uint8_t {
    ShaderResource,
    RenderTarget,
    DepthStencil
};

class ResourceView {
public:
    struct Resolved {
        Diligent::ITexture* texture{ nullptr };
        Diligent::ITextureView* view{ nullptr };
    };

    ResourceView() = default;
    ResourceView(Diligent::ITexture* texture, ResourceViewType type) noexcept;
    ~ResourceView();

    ResourceView(const ResourceView&) = delete;
    ResourceView& operator=(const ResourceView&) = delete;
    ResourceView(ResourceView&& other) noexcept;
    ResourceView& operator=(ResourceView&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept { return m_texture && m_view; }
    [[nodiscard]] ResourceViewType GetType() const noexcept { return m_type; }
    [[nodiscard]] Resolved Resolve() const noexcept { return { m_texture, m_view }; }

private:
    void Reset() noexcept;

    Diligent::ITexture* m_texture{ nullptr };
    Diligent::ITextureView* m_view{ nullptr };
    ResourceViewType m_type{ ResourceViewType::ShaderResource };
};

} // namespace elm::render