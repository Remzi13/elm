#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>

namespace elm {
class RenderSystem;
struct ImGuiFrame;

namespace render {
class Texture;

class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint64_t RenderFrame(RenderSystem& renderSystem, ImGuiFrame& frame);
    void ReleaseViewportSurfaces(RenderSystem& renderSystem);
    void Shutdown();

private:
    [[nodiscard]] bool InitializeTextures();

    std::unique_ptr<Texture> m_fontTexture;
    std::unique_ptr<Texture> m_whiteTexture;
    std::unordered_set<uint32_t> m_viewportSurfaceIds;
};

} // namespace render
} // namespace elm
