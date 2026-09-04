#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

namespace Engine {

class DepthPreviewWindow final : public IImGuiWindow {
public:
    DepthPreviewWindow() = default;

    [[nodiscard]] std::string_view GetName() const override { return "Software Depth Buffer Viewport"; }
    void Render(RenderSystem& renderSystem, const FrameStats& stats) override;
};

} // namespace Engine
