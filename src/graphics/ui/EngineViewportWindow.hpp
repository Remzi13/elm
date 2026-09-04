#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

namespace elm  {

class EngineViewportWindow final : public IImGuiWindow {
public:
    [[nodiscard]] StringView GetName() const override { return "Engine Viewport"; }
    void Render(RenderSystem& renderSystem, const FrameStats& stats) override;
};

} // namespace Engine
