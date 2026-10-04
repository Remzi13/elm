#include "render/SwapChain.hpp"

#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#include <utility>

namespace elm::render {

SwapChain::SwapChain(Diligent::ISwapChain* swapChain) noexcept
    : m_swapChain(swapChain)
{
}

SwapChain::~SwapChain()
{
    Reset();
}

SwapChain::SwapChain(SwapChain&& other) noexcept
    : m_swapChain(std::exchange(other.m_swapChain, nullptr))
{
}

SwapChain& SwapChain::operator=(SwapChain&& other) noexcept
{
    if (this != &other) {
        Reset(std::exchange(other.m_swapChain, nullptr));
    }
    return *this;
}

SwapChain::operator bool() const noexcept
{
    return m_swapChain != nullptr;
}

const Diligent::SwapChainDesc& SwapChain::GetDesc() const
{
    return m_swapChain->GetDesc();
}

Diligent::ITextureView* SwapChain::GetCurrentBackBufferRTV() const
{
    return m_swapChain ? m_swapChain->GetCurrentBackBufferRTV() : nullptr;
}

Diligent::ITextureView* SwapChain::GetDepthBufferDSV() const
{
    return m_swapChain ? m_swapChain->GetDepthBufferDSV() : nullptr;
}

void SwapChain::ResizeIfNeeded(uint32_t width, uint32_t height)
{
    if (!m_swapChain)
        return;

    const auto& description = m_swapChain->GetDesc();
    if (description.Width != width || description.Height != height)
        m_swapChain->Resize(width, height);
}

void SwapChain::Present()
{
    if (m_swapChain)
        m_swapChain->Present();
}

void SwapChain::Reset(Diligent::ISwapChain* swapChain) noexcept
{
    if (m_swapChain)
        m_swapChain->Release();
    m_swapChain = swapChain;
}

} // namespace elm::render