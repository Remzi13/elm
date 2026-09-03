#pragma once

#include "core/Error.hpp"
#include "Scene/Transform.hpp"
#include "graphics/culling/MathTypes.hpp"
#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "graphics/Camera.hpp"
#include "Scene/TestScenes.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace Diligent {
    class IRenderDevice;
    class IDeviceContext;
    class ISwapChain;
    class IPipelineState;
    class IShaderResourceBinding;
    class IBuffer;
    class ITexture;
    class ITextureView;
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

struct GpuInstanceData {
    Matrix4x4 World;
    Vector4 Color;
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

    void Update(float deltaTime);
    void BeginFrame();
    void RenderScene();
    void RenderUI(const FrameStats& stats);
    void EndFrame();
    void Shutdown();

    [[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }
    [[nodiscard]] Camera& GetCamera() { return m_camera; }
    [[nodiscard]] OcclusionCullingSystem& GetCullingSystem() { return m_cullingSystem; }

private:
    void InitPipeline();
    void CreateMeshBuffers();
    void CreateInstanceBuffer();
    void CreateDepthPreviewTexture(uint32_t width, uint32_t height);
    void CreateEngineViewport(uint32_t width, uint32_t height);
    void UpdateDepthPreviewTexture();
    void RebuildScene();

private:
    GLFWwindow* m_window{nullptr};

    // Diligent Engine components
    Diligent::IRenderDevice* m_renderDevice{nullptr};
    Diligent::IDeviceContext* m_deviceContext{nullptr};
    Diligent::ISwapChain* m_swapChain{nullptr};

    // Shaders & Pipelines
    Diligent::IPipelineState* m_pPSO{nullptr};
    Diligent::IPipelineState* m_pHighlightPSO{nullptr};
    Diligent::IShaderResourceBinding* m_pSRB{nullptr};
    Diligent::IBuffer* m_pCameraConstantsBuffer{nullptr};

    // Geometry Buffers
    Diligent::IBuffer* m_pCubeVB{nullptr};
    Diligent::IBuffer* m_pCubeIB{nullptr};
    uint32_t m_cubeIndexCount{0};

    Diligent::IBuffer* m_pWallVB{nullptr};
    Diligent::IBuffer* m_pWallIB{nullptr};
    uint32_t m_wallIndexCount{0};

    Diligent::IBuffer* m_pGroundVB{nullptr};
    Diligent::IBuffer* m_pGroundIB{nullptr};
    uint32_t m_groundIndexCount{0};

    // Dynamic Instance Buffer
    Diligent::IBuffer* m_pInstanceBuffer{nullptr};
    static constexpr size_t MaxInstances = 30000;

    // Depth Buffer Visualization Texture
    Diligent::ITexture* m_pDepthPreviewTex{nullptr};
    Diligent::ITextureView* m_pDepthPreviewSRV{nullptr};
    uint32_t m_depthPreviewWidth{256};
    uint32_t m_depthPreviewHeight{144};
    std::vector<uint32_t> m_depthPreviewPixels;
    bool m_depthPreviewFalseColor{true};

    // Offscreen render target displayed inside the dockspace.
    Diligent::ITexture* m_pEngineViewportTex{nullptr};
    Diligent::ITextureView* m_pEngineViewportRTV{nullptr};
    Diligent::ITextureView* m_pEngineViewportDSV{nullptr};
    Diligent::ITextureView* m_pEngineViewportSRV{nullptr};
    uint32_t m_engineViewportWidth{1280};
    uint32_t m_engineViewportHeight{720};
    bool m_engineViewportIsShaderResource{false};

    // Systems & Scenes
    Camera m_camera;
    OcclusionCullingSystem m_cullingSystem;

    ScenePreset m_currentPreset{ScenePreset::WallAndGrid};
    int m_targetInstanceCount{1500};

    std::vector<OccluderInstance> m_occluders;
    std::vector<OccludeeInstance> m_occludees;
    std::vector<GpuInstanceData> m_visibleGpuInstances;
    std::vector<GpuInstanceData> m_culledGpuInstances;

    std::unique_ptr<Diligent::ImGuiImplDiligent> m_imGui;
    bool m_imguiContextCreated{false};

    uint32_t m_width{1280};
    uint32_t m_height{720};
    bool m_initialized{false};
};

} // namespace Engine
