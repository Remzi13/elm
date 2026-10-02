#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include <array>

namespace elm {

    class LogWindow final : public IImGuiWindow
    {
    public:
        using IImGuiWindow::IImGuiWindow;

        [[nodiscard]] StringView GetName() const override { return "Log"; }
        void Render(RenderSystem& renderSystem, Scene& scene, const Camera& camera, const FrameStats& stats) override;

    private:
        std::array<bool, 4> m_severityEnabled{ true, true, true, true };
        std::array<bool, 4> m_categoryEnabled{ true, true, true, true };
        int m_maxMessages{ 5000 };
        bool m_autoScroll{ true };
    };

} // namespace elm
