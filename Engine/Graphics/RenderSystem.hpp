#pragma once

#include "Engine/Core/Error.hpp"
#include "Engine/Scene/Transform.hpp"

#include <memory>
#include <string_view>

struct GLFWwindow;

namespace Diligent {
    class IRenderDevice;
    class IDeviceContext;
    class ISwapChain;
    class ImGuiImplDiligent;
}

namespace Engine {

struct FrameStats {
    float fps{0.0f};
    float deltaTimeMs{0.0f};
    uint32_t physicsBodyCount{0};
    Transform boxTransform;
    Transform groundTransform;
};

class RenderSystem {
public:
    RenderSystem();
    ~RenderSystem();

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) noexcept = delete;
    RenderSystem& operator=(RenderSystem&&) noexcept = delete;

    [[nodiscard]] auto Init(uint32_t width, uint32_t height, std::string_view title) -> EngineResult<void>;
    
    [[nodiscard]] bool ShouldClose() const;
    void PollEvents();

    void BeginFrame();
    void RenderUI(const FrameStats& stats);
    void EndFrame();
    void Shutdown();

    [[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }

private:
    GLFWwindow* m_window{nullptr};

    // Diligent Engine components
    Diligent::IRenderDevice* m_renderDevice{nullptr};
    Diligent::IDeviceContext* m_deviceContext{nullptr};
    Diligent::ISwapChain* m_swapChain{nullptr};

    std::unique_ptr<Diligent::ImGuiImplDiligent> m_imGui;
    bool m_imguiContextCreated{false};

    uint32_t m_width{1280};
    uint32_t m_height{720};
    bool m_initialized{false};
};

} // namespace Engine
