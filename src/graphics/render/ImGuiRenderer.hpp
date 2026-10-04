#pragma once

#include "core/Handler.hpp"

#include <cstdint>
#include <memory>

namespace elm {
class RenderSystem;

namespace render {
class Texture;

class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] core::Handler GetFallbackTexture() const noexcept;
    void Shutdown();

private:
    [[nodiscard]] bool InitializeTextures();

    std::unique_ptr<Texture> m_fontTexture;
    std::unique_ptr<Texture> m_whiteTexture;
};

} // namespace render
} // namespace elm
