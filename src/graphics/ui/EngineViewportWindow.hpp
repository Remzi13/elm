#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"
namespace elm  {

class EngineViewportWindow final : public IImGuiWindow {
public:
    using IImGuiWindow::IImGuiWindow;

    [[nodiscard]] StringView GetName() const override { return "Engine Viewport"; }
    void Render(ImGuiWindowContext& context) override;
};

} // namespace Engine
