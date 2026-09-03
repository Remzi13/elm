#pragma once

#include "core/Error.hpp"
#include "graphics/RenderSystem.hpp"

#include <memory>
#include <string_view>

namespace Diligent {
class ImGuiImplDiligent;
}

namespace Engine {

class ImGuiSystem {
public:
    ImGuiSystem();
    ~ImGuiSystem();

    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;

    [[nodiscard]] auto Init(RenderSystem& renderSystem, std::string_view title) -> EngineResult<void>;
    void BeginFrame(RenderSystem& renderSystem);
    void Render(RenderSystem& renderSystem, const FrameStats& stats);
    void Shutdown();

private:
    void RenderDockspace();
    void RenderEngineViewport(RenderSystem& renderSystem);
    void RenderControls(RenderSystem& renderSystem, const FrameStats& stats);
    void RenderDepthPreview(RenderSystem& renderSystem);

private:
    GLFWwindow* m_window{nullptr};
    std::unique_ptr<Diligent::ImGuiImplDiligent> m_imGui;
    bool m_initialized{false};
    int m_currentResolution{2};
    std::string m_title;
};

} // namespace Engine
