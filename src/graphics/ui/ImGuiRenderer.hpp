#pragma once

#include "core/Std.hpp"

#include "core/Handler.hpp"

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

    UniquePtr<Texture> m_fontTexture;
    UniquePtr<Texture> m_whiteTexture;
};

} // namespace render
} // namespace elm
