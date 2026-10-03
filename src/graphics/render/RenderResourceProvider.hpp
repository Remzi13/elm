#pragma once

#include "core/Handler.hpp"
#include "graphics/render/ResourceView.hpp"
#include "graphics/render/SwapChain.hpp"

#include <cstdint>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
}

namespace elm::render {
struct TextureInfo;
struct TextureData;
class TextureStore;

class RenderResourceProvider {
public:
    RenderResourceProvider(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext,
        TextureStore& textureStore) noexcept;
    ~RenderResourceProvider();

    RenderResourceProvider(const RenderResourceProvider&) = delete;
    RenderResourceProvider& operator=(const RenderResourceProvider&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool HasTexture(const core::Handler& handler) const;
    [[nodiscard]] ResourceView GetShaderResourceView(const core::Handler& handler) const;
    [[nodiscard]] core::Handler CreateTexture(const core::Handler& handler,
        const TextureInfo& info);
    [[nodiscard]] ResourceView CreateRenderTexture(const core::Handler& handler,
        uint32_t width, uint32_t height, ResourceViewType viewType);
    void UpdateTexture(const core::Handler& handler, const TextureData& textureData);
    void ReleaseTexture(const core::Handler& handler);
    void ClearTextures();

    [[nodiscard]] SwapChain* GetMainSwapChain() const noexcept;
    void SetMainSwapChain(SwapChain* swapChain) noexcept;
    [[nodiscard]] SwapChain CreateSwapChain(uint32_t width, uint32_t height, void* nativeHandle,
        void* nativeDisplay, bool withDepthBuffer = true);

private:
    Diligent::IRenderDevice* m_renderDevice;
    Diligent::IDeviceContext* m_deviceContext;
    TextureStore& m_textureStore;
    SwapChain* m_mainSwapChain{ nullptr };
};

} // namespace elm::render
