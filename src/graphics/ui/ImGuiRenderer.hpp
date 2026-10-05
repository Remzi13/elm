#pragma once

#include "core/Std.hpp"

#include "render/api/RenderResources.hpp"

namespace elm::render {

/// Owns the GPU textures the UI needs (font atlas, white fallback).
class ImGuiRenderer {
public:
    explicit ImGuiRenderer(RenderResources& resources);
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] TextureHandle GetFallbackTexture() const noexcept;
    void Shutdown();

private:
    bool InitializeTextures(RenderResources& resources);

    Texture m_fontTexture;
    Texture m_whiteTexture;
};

} // namespace elm::render
