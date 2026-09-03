#include "graphics/ImGuiSystem.hpp"

#include <GLFW/glfw3.h>

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"
#include "imgui.h"

namespace Engine {

ImGuiSystem::ImGuiSystem() = default;

ImGuiSystem::~ImGuiSystem() {
    Shutdown();
}

auto ImGuiSystem::Init(RenderSystem& renderSystem, std::string_view title) -> EngineResult<void> {
    m_window = renderSystem.GetWindowHandle();
    m_title = title;

    const auto& swapChainDesc = renderSystem.GetSwapChain()->GetDesc();
    Diligent::ImGuiDiligentCreateInfo createInfo;
    createInfo.pDevice = renderSystem.GetRenderDevice();
    createInfo.BackBufferFmt = swapChainDesc.ColorBufferFormat;
    createInfo.DepthBufferFmt = swapChainDesc.DepthBufferFormat;
    m_imGui = std::make_unique<Diligent::ImGuiImplDiligent>(createInfo);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    m_glfwInitialized = ImGui_ImplGlfw_InitForOther(m_window, true);
    if (!m_glfwInitialized) {
        Shutdown();
        return std::unexpected(EngineError(ErrorCode::UnknownError, "Failed to initialize ImGui GLFW backend"));
    }
    m_initialized = true;
    return {};
}

void ImGuiSystem::BeginFrame(RenderSystem& renderSystem) {
    if (!m_initialized) return;
    ImGui_ImplGlfw_NewFrame();
    const auto& swapChainDesc = renderSystem.GetSwapChain()->GetDesc();
    m_imGui->NewFrame(renderSystem.GetWidth(), renderSystem.GetHeight(), swapChainDesc.PreTransform);
}

void ImGuiSystem::RenderDockspace() {
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->Pos);
    ImGui::SetNextWindowSize(mainViewport->Size);
    ImGui::SetNextWindowViewport(mainViewport->ID);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("DockSpaceHost", nullptr, flags);
    ImGui::PopStyleVar(2);
    ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void ImGuiSystem::RenderEngineViewport(RenderSystem& renderSystem) {
    ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowDockID(ImGui::GetID("MainDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Viewport");
    if (auto* texture = renderSystem.GetEngineViewportSRV()) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(renderSystem.GetEngineViewportWidth()) /
                             static_cast<float>(renderSystem.GetEngineViewportHeight());
        ImVec2 size = available;
        if (size.x / aspect < size.y) size.y = size.x / aspect;
        else size.x = size.y * aspect;
        ImGui::Image(reinterpret_cast<ImTextureID>(texture), size);
    }
    ImGui::End();
}

void ImGuiSystem::RenderControls(RenderSystem& renderSystem, const FrameStats& stats) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("Software Occlusion Culling Lab");
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
    ImGui::Text("FPS: %.1f | Frame: %.2f ms", stats.fps, stats.deltaTimeMs);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Scene Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* presets[] = {"The Great Wall & City Grid", "Rooms & Corridors", "Physics Barrier Sandbox"};
        int preset = static_cast<int>(renderSystem.m_currentPreset);
        if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
            renderSystem.m_currentPreset = static_cast<ScenePreset>(preset);
            renderSystem.RebuildScene();
        }
        if (ImGui::SliderInt("Instances", &renderSystem.m_targetInstanceCount, 100, 10000)) {
            renderSystem.RebuildScene();
        }
    }

    if (ImGui::CollapsingHeader("Culling Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& culling = renderSystem.m_cullingSystem;
        ImGui::Checkbox("Enable Frustum Culling", &culling.enableFrustumCulling);
        ImGui::Checkbox("Enable Software Occlusion Culling", &culling.enableOcclusionCulling);
        bool freeze = renderSystem.m_camera.IsCullingFrozen();
        if (ImGui::Checkbox("Freeze Culling Camera", &freeze)) renderSystem.m_camera.SetFreezeCulling(freeze);
        if (freeze) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[FROZEN]"); }
        ImGui::SliderFloat("Depth Bias", &culling.depthBias, 0.0f, 0.01f, "%.4f");
        const char* resolutions[] = {"64x36", "128x72", "256x144 (Recommended)", "320x180", "512x288"};
        if (ImGui::Combo("SOC Buffer Res", &m_currentResolution, resolutions, IM_ARRAYSIZE(resolutions))) {
            const uint32_t widths[] = {64, 128, 256, 320, 512};
            const uint32_t heights[] = {36, 72, 144, 180, 288};
            culling.SetResolution(widths[m_currentResolution], heights[m_currentResolution]);
            renderSystem.CreateDepthPreviewTexture(widths[m_currentResolution], heights[m_currentResolution]);
        }
    }

    if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int mode = static_cast<int>(renderSystem.m_cullingSystem.visualMode);
        ImGui::RadioButton("Hide Culled (Draw Visible Only)", &mode, 0);
        ImGui::RadioButton("Highlight Culled (Red Ghost)", &mode, 1);
        ImGui::RadioButton("Occluders Only", &mode, 2);
        renderSystem.m_cullingSystem.visualMode = static_cast<VisualMode>(mode);
    }

    if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& s = renderSystem.m_cullingSystem.GetStats();
        ImGui::Text("Total Objects:     %u", s.totalObjects);
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", s.visibleCount);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", s.frustumCulledCount);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", s.occlusionCulledCount);
        ImGui::Spacing();
        ImGui::ProgressBar(s.cullingRatioPercent / 100.0f, ImVec2(-1, 0), "");
        ImGui::SameLine(); ImGui::Text("Culled: %.1f%%", s.cullingRatioPercent);
        ImGui::Separator();
        ImGui::Text("CPU Rasterize Time: %.1f us (%.3f ms)", s.rasterizeTimeUs, s.rasterizeTimeUs / 1000.0f);
        ImGui::Text("CPU Query / Test:   %.1f us (%.3f ms)", s.queryTimeUs, s.queryTimeUs / 1000.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time: %.1f us (%.3f ms)", s.totalCullingTimeUs, s.totalCullingTimeUs / 1000.0f);
    }

    if (ImGui::CollapsingHeader("Camera & Controls")) {
        const Vector3 pos = renderSystem.m_camera.GetPosition();
        ImGui::Text("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
        ImGui::SliderFloat("Move Speed", &renderSystem.m_camera.moveSpeed, 5.0f, 50.0f);
        ImGui::Separator(); ImGui::TextDisabled("Controls:");
        ImGui::BulletText("Right Mouse Button + Drag: Look around");
        ImGui::BulletText("W / A / S / D: Move forward / left / back / right");
        ImGui::BulletText("E / Q: Move Up / Down");
        ImGui::BulletText("Left Shift: Sprint (2.5x speed)");
    }
    ImGui::End();
}

void ImGuiSystem::RenderDepthPreview(RenderSystem& renderSystem) {
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(renderSystem.GetWidth()) - 460.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
    ImGui::Begin("Software Depth Buffer Viewport");
    ImGui::Text("Resolution: %ux%u", renderSystem.m_depthPreviewWidth, renderSystem.m_depthPreviewHeight);
    ImGui::SameLine(); ImGui::Checkbox("False Color (Heatmap)", &renderSystem.m_depthPreviewFalseColor);
    if (auto* texture = renderSystem.m_pDepthPreviewSRV) {
        const float aspect = static_cast<float>(renderSystem.m_depthPreviewWidth) / renderSystem.m_depthPreviewHeight;
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = width / aspect;
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(width, height));
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const uint32_t px = static_cast<uint32_t>(((mouse.x - imagePos.x) / width) * renderSystem.m_depthPreviewWidth);
            const uint32_t py = static_cast<uint32_t>(((mouse.y - imagePos.y) / height) * renderSystem.m_depthPreviewHeight);
            if (px < renderSystem.m_depthPreviewWidth && py < renderSystem.m_depthPreviewHeight) {
                ImGui::BeginTooltip();
                ImGui::Text("Pixel: (%u, %u)", px, py);
                ImGui::Text("Normalized Depth: %.4f", renderSystem.m_cullingSystem.GetDepthBuffer().GetDepth(px, py));
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::End();
}

void ImGuiSystem::Render(RenderSystem& renderSystem, const FrameStats& stats) {
    if (!m_initialized) return;
    RenderDockspace();
    RenderEngineViewport(renderSystem);
    RenderControls(renderSystem, stats);
    RenderDepthPreview(renderSystem);

    auto* context = renderSystem.GetDeviceContext();
    auto* swapChain = renderSystem.GetSwapChain();
    auto* rtv = swapChain->GetCurrentBackBufferRTV();
    auto* dsv = swapChain->GetDepthBufferDSV();
    context->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_imGui->Render(context);
    ImGui::UpdatePlatformWindows();
}

void ImGuiSystem::Shutdown() {
    if (m_glfwInitialized) {
        ImGui_ImplGlfw_Shutdown();
        m_glfwInitialized = false;
    }
    m_imGui.reset();
    m_initialized = false;
}

} // namespace Engine
