#pragma once

#include "graphics/render/SwapChain.hpp"

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
}

namespace elm::render {
class TextureStore;

class RenderResourceProvider {
public:
    RenderResourceProvider(Diligent::IRenderDevice* renderDevice, Diligent::IDeviceContext* deviceContext,
        TextureStore& textureStore) noexcept;

    RenderResourceProvider(const RenderResourceProvider&) = delete;
    RenderResourceProvider& operator=(const RenderResourceProvider&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] Diligent::IRenderDevice* GetRenderDevice() const noexcept;
    [[nodiscard]] Diligent::IDeviceContext* GetDeviceContext() const noexcept;
    [[nodiscard]] TextureStore& GetTextureStore() const noexcept;
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
