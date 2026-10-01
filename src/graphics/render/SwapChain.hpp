#pragma once

#include <cstdint>

namespace Diligent {
class ISwapChain;
class ITextureView;
struct SwapChainDesc;
}

namespace elm::render {

class SwapChain {
public:
    SwapChain() = default;
    explicit SwapChain(Diligent::ISwapChain* swapChain) noexcept;
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;
    SwapChain(SwapChain&& other) noexcept;
    SwapChain& operator=(SwapChain&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] const Diligent::SwapChainDesc& GetDesc() const;
    [[nodiscard]] Diligent::ITextureView* GetCurrentBackBufferRTV() const;
    [[nodiscard]] Diligent::ITextureView* GetDepthBufferDSV() const;
    void ResizeIfNeeded(uint32_t width, uint32_t height);
    void Present();
    void Reset(Diligent::ISwapChain* swapChain = nullptr) noexcept;

private:
    Diligent::ISwapChain* m_swapChain{ nullptr };
};

} // namespace elm::render