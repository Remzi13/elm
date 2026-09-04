#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

namespace Engine {

class EngineViewportWindow final : public IImGuiWindow {
public:
    [[nodiscard]] std::string_view GetName() const override { return "Engine Viewport"; }
    void Render(RenderSystem& renderSystem, const FrameStats& stats) override;
};

} // namespace Engine
