#pragma once

#include "core/Error.hpp"
#include "graphics/RenderSystem.hpp"
#include "imgui.h"

#include <memory>
#include <string>
#include <string_view>

namespace Diligent {
class ImGuiImplDiligentViewport;
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
    static void CreateViewport(ImGuiViewport* viewport);
    static void DestroyViewport(ImGuiViewport* viewport);
    static void ResizeViewport(ImGuiViewport* viewport, ImVec2 size);
    static float GetViewportDpiScale(ImGuiViewport* viewport);
    static void RenderViewport(ImGuiViewport* viewport, void* userData);
    static void PresentViewport(ImGuiViewport* viewport, void* userData);

    struct SavedLabSettings {
        int preset{0};
        int instanceCount{1000};
        bool enableFrustum{true};
        bool enableOcclusion{true};
        float depthBias{0.0f};
        int resolution{2};
        int visualMode{0};
        bool depthFalseColor{true};
        float moveSpeed{10.0f};
        bool hasLoaded{false};
    };

    GLFWwindow* m_window{nullptr};
    std::unique_ptr<Diligent::ImGuiImplDiligentViewport> m_imGui;
    RenderSystem* m_renderSystem{nullptr};
    bool m_initialized{false};
    bool m_glfwInitialized{false};
    int m_currentResolution{2};
    std::string m_title;
    std::string m_iniFilePath;
    SavedLabSettings m_savedSettings;
};

} // namespace Engine
