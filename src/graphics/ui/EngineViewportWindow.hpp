#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"
namespace elm  {

class EngineViewportWindow final : public IImGuiWindow {
public:
    using IImGuiWindow::IImGuiWindow;

    [[nodiscard]] StringView GetName() const override { return "Engine Viewport"; }
    void Render(ImGuiWindowContext& context) override;

    // State of the last built frame, used to route camera input to the viewport only
    [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
    [[nodiscard]] bool IsFocused() const noexcept { return m_focused; }

private:
    bool m_hovered { false };
    bool m_focused { false };
};

} // namespace Engine
