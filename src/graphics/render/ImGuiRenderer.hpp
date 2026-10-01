#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Diligent {
class ImGuiImplDiligentViewport;
}

namespace elm {
class RenderSystem;
struct ImGuiFrame;
struct ImGuiViewportSnapshot;
struct ImGuiViewportEvent;

namespace render {
class RenderResourceProvider;
class SwapChain;

class ImGuiRenderer {
public:
    ImGuiRenderer(RenderSystem& renderSystem, RenderResourceProvider& resourceProvider);
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint64_t RenderFrame(ImGuiFrame& frame);
    void ReleaseViewportSwapChains();
    void Shutdown();

private:
    [[nodiscard]] SwapChain CreateViewportSwapChain(const ImGuiViewportEvent& event);
    void ApplyViewportEvents(const ImGuiFrame& frame, uint64_t& releasedViewportCount);
    void ResolveTextureIds(ImGuiViewportSnapshot& snapshot);

    RenderSystem& m_renderSystem;
    RenderResourceProvider& m_resourceProvider;
    SwapChain* m_mainSwapChain;
    std::unique_ptr<Diligent::ImGuiImplDiligentViewport> m_imguiRenderer;
    std::unordered_map<uint32_t, SwapChain> m_viewportSwapChains;
};

} // namespace render
} // namespace elm
