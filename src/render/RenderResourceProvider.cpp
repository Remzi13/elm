#include "render/RenderResourceProvider.hpp"

#include "render/Texture.hpp"
#include "render/TextureStore.hpp"
#include "render/backends/Utils.hpp"
#include "render/Render.hpp"

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"
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

RenderResourceProvider::~RenderResourceProvider() = default;

bool RenderResourceProvider::IsInitialized() const noexcept
{
    return m_renderDevice && m_deviceContext;
}

bool RenderResourceProvider::HasTexture(const core::Handler& handler) const
{
    const auto* data = m_textureStore.Find(handler);
    return data && data->pTexture;
}

ResourceView RenderResourceProvider::GetShaderResourceView(const core::Handler& handler) const
{
    const auto* data = m_textureStore.Find(handler);
    return data && data->pTexture
        ? ResourceView(data->pTexture, ResourceViewType::ShaderResource)
        : ResourceView{};
}

core::Handler RenderResourceProvider::CreateTexture(const core::Handler& handler,
    const TextureInfo& info)
{
    Diligent::TextureDesc description;
    description.Name = info.name.c_str();
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = info.width;
    description.Height = info.height;
    description.Format = getTextureFormat(info.format);
    description.Usage = getUsage(info.usage);
    description.BindFlags = getTextureBindFlags(info.bindFlags);
    return m_textureStore.Create(m_renderDevice, handler, description);
}

ResourceView RenderResourceProvider::CreateRenderTexture(const core::Handler& handler,
    uint32_t width, uint32_t height, ResourceViewType viewType)
{
    if (!m_mainSwapChain || width == 0 || height == 0)
        return {};

    Diligent::TextureDesc description;
    description.Name = viewType == ResourceViewType::DepthStencil
        ? "Engine Viewport Depth"
        : "Engine Viewport Color";
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = width;
    description.Height = height;
    description.Usage = Diligent::USAGE_DEFAULT;
    if (viewType == ResourceViewType::DepthStencil) {
        description.Format = m_mainSwapChain->GetDesc().DepthBufferFormat;
        description.BindFlags = Diligent::BIND_DEPTH_STENCIL;
    } else {
        description.Format = m_mainSwapChain->GetDesc().ColorBufferFormat;
        description.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
    }

    const auto created = m_textureStore.Create(m_renderDevice, handler, description);
    if (!created.IsValid())
        return {};

    const auto* data = m_textureStore.Find(created);
    if (!data || !data->pTexture) {
        ReleaseTexture(created);
        return {};
    }

    ResourceView view(data->pTexture, viewType);
    if (!view.IsValid()) {
        ReleaseTexture(created);
        return {};
    }

    data->pTexture->SetState(viewType == ResourceViewType::DepthStencil
        ? Diligent::RESOURCE_STATE_DEPTH_WRITE
        : Diligent::RESOURCE_STATE_RENDER_TARGET);
    return view;
}

void RenderResourceProvider::UpdateTexture(const core::Handler& handler, const TextureData& textureData)
{
    if (!m_deviceContext)
        return;

    const auto* data = m_textureStore.Find(handler);
    if (!data || !data->pTexture)
        return;

    Diligent::Box updateBox;
    updateBox.MinX = 0;
    updateBox.MaxX = data->width;
    updateBox.MinY = 0;
    updateBox.MaxY = data->height;

    Diligent::TextureSubResData subresData;
    subresData.Stride = textureData.stride > 0
        ? textureData.stride
        : data->width * sizeof(uint8_t);
    subresData.pData = textureData.data.data();

    m_deviceContext->UpdateTexture(data->pTexture, 0, 0, updateBox, subresData,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void RenderResourceProvider::ReleaseTexture(const core::Handler& handler)
{
    m_textureStore.Release(handler);
}

void RenderResourceProvider::ClearTextures()
{
    m_textureStore.Clear();
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
