#include "graphics/render/RenderResourceProvider.hpp"

#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif

namespace elm::render {

RenderResourceProvider::RenderResourceProvider(Diligent::IRenderDevice* renderDevice,
    Diligent::IDeviceContext* deviceContext, TextureStore& textureStore) noexcept
    : m_renderDevice(renderDevice)
    , m_deviceContext(deviceContext)
    , m_textureStore(textureStore)
{
}

bool RenderResourceProvider::IsInitialized() const noexcept
{
    return m_renderDevice && m_deviceContext;
}

Diligent::IRenderDevice* RenderResourceProvider::GetRenderDevice() const noexcept
{
    return m_renderDevice;
}

Diligent::IDeviceContext* RenderResourceProvider::GetDeviceContext() const noexcept
{
    return m_deviceContext;
}

TextureStore& RenderResourceProvider::GetTextureStore() const noexcept
{
    return m_textureStore;
}

SwapChain* RenderResourceProvider::GetMainSwapChain() const noexcept
{
    return m_mainSwapChain;
}

void RenderResourceProvider::SetMainSwapChain(SwapChain* swapChain) noexcept
{
    m_mainSwapChain = swapChain;
}

SwapChain RenderResourceProvider::CreateSwapChain(uint32_t width, uint32_t height, void* nativeHandle,
    void* nativeDisplay, bool withDepthBuffer)
{
    if (!IsInitialized() || !nativeHandle)
        return {};

    Diligent::SwapChainDesc description = m_mainSwapChain
        ? m_mainSwapChain->GetDesc()
        : Diligent::SwapChainDesc{};
    description.Width = width;
    description.Height = height;
    if (!withDepthBuffer)
        description.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;

    Diligent::ISwapChain* swapChain = nullptr;
#if PLATFORM_WIN32
    auto* factory = Diligent::GetEngineFactoryD3D12();
    if (!factory)
        return {};

    Diligent::Win32NativeWindow nativeWindow{ nativeHandle };
    factory->CreateSwapChainD3D12(m_renderDevice, m_deviceContext, description,
        Diligent::FullScreenModeDesc{}, nativeWindow, &swapChain);
#else
    auto* factory = Diligent::GetEngineFactoryVk();
    if (!factory)
        return {};

    Diligent::LinuxNativeWindow nativeWindow;
    nativeWindow.pDisplay = nativeDisplay;
    nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(nativeHandle));
    factory->CreateSwapChainVk(m_renderDevice, m_deviceContext, description, nativeWindow, &swapChain);
#endif

    return SwapChain(swapChain);
}

} // namespace elm::render
