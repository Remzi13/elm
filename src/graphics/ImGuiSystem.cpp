#include "graphics/ImGuiSystem.hpp"
#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ui/SocLabWindow.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "imgui_internal.h"

#if PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#endif
#include "ImGuiDiligentRenderer.hpp"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"
#include "imgui.h"

namespace Diligent {

class ImGuiImplDiligentViewport : public ImGuiImplDiligent {
public:
    using ImGuiImplDiligent::ImGuiImplDiligent;

    void SetRenderSurface(Uint32 width, Uint32 height, SURFACE_TRANSFORM transform) {
        m_pRenderer->NewFrame(width, height, transform);
    }

    void RenderDrawData(IDeviceContext* context, ImDrawData* drawData) {
        m_pRenderer->RenderDrawData(context, drawData);
    }
};

} // namespace Diligent

namespace elm  {

struct ImGuiViewportRendererData {
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;
};

ImGuiSystem::ImGuiSystem() = default;

ImGuiSystem::~ImGuiSystem() {
    Shutdown();
}

void ImGuiSystem::AddWindow(std::unique_ptr<IImGuiWindow> window) {
    if (window) {
        m_windows.push_back(std::move(window));
    }
}

auto ImGuiSystem::Init(RenderSystem& renderSystem, StringView title) -> elm::EngineResult<void> {
    m_window = renderSystem.GetWindowHandle();
    m_renderSystem = &renderSystem;
    m_title = title;

    // Register standard engine editor windows
    auto socLab = std::make_unique<SocLabWindow>();
    m_socLabWindow = socLab.get();
    AddWindow(std::move(socLab));
    AddWindow(std::make_unique<EngineViewportWindow>());
    AddWindow(std::make_unique<DepthPreviewWindow>());

    const auto& swapChainDesc = renderSystem.GetSwapChain()->GetDesc();
    Diligent::ImGuiDiligentCreateInfo createInfo;
    createInfo.pDevice = renderSystem.GetRenderDevice();
    createInfo.BackBufferFmt = swapChainDesc.ColorBufferFormat;
    createInfo.DepthBufferFmt = Diligent::TEX_FORMAT_UNKNOWN;
    m_imGui = std::make_unique<Diligent::ImGuiImplDiligentViewport>(createInfo);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable |
        ImGuiConfigFlags_DpiEnableScaleViewports | ImGuiConfigFlags_DpiEnableScaleFonts;
    io.ConfigViewportsNoAutoMerge = false;
    io.UserData = this;

    const std::filesystem::path sourceRoot =
        std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
    if (std::filesystem::exists(sourceRoot / "CMakeLists.txt")) {
        m_iniFilePath = (sourceRoot / "imgui.ini").string();
    } else {
        m_iniFilePath = "imgui.ini";
    }
    io.IniFilename = m_iniFilePath.c_str();

    ImGuiSettingsHandler iniHandler;
    iniHandler.TypeName = "LabSettings";
    iniHandler.TypeHash = ImHashStr("LabSettings");
    iniHandler.UserData = this;
    iniHandler.ClearAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
        auto* self = static_cast<ImGuiSystem*>(handler->UserData);
        if (self) self->m_savedSettings = {};
    };
    iniHandler.ReadInitFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
        auto* self = static_cast<ImGuiSystem*>(handler->UserData);
        if (self) self->m_savedSettings = {};
    };
    iniHandler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) -> void* {
        if (std::strcmp(name, "State") == 0) {
            return handler->UserData;
        }
        return nullptr;
    };
    iniHandler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
        auto* self = static_cast<ImGuiSystem*>(entry);
        if (!self) return;
        int val = 0;
        float fval = 0.0f;
        if (std::sscanf(line, "Preset=%d", &val) == 1) {
            self->m_savedSettings.preset = val;
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "Instances=%d", &val) == 1) {
            self->m_savedSettings.instanceCount = val;
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "FrustumCulling=%d", &val) == 1) {
            self->m_savedSettings.enableFrustum = (val != 0);
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "OcclusionCulling=%d", &val) == 1) {
            self->m_savedSettings.enableOcclusion = (val != 0);
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "DepthBias=%f", &fval) == 1) {
            self->m_savedSettings.depthBias = fval;
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "SOCResolution=%d", &val) == 1) {
            self->m_savedSettings.resolution = val;
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "VisualMode=%d", &val) == 1) {
            self->m_savedSettings.visualMode = val;
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "DepthFalseColor=%d", &val) == 1) {
            self->m_savedSettings.depthFalseColor = (val != 0);
            self->m_savedSettings.hasLoaded = true;
        } else if (std::sscanf(line, "MoveSpeed=%f", &fval) == 1) {
            self->m_savedSettings.moveSpeed = fval;
            self->m_savedSettings.hasLoaded = true;
        }
    };
    iniHandler.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) {
        auto* self = static_cast<ImGuiSystem*>(handler->UserData);
        if (!self || !self->m_savedSettings.hasLoaded || !self->m_renderSystem) return;
        auto& rs = *self->m_renderSystem;
        rs.SetCurrentPreset(static_cast<ScenePreset>(self->m_savedSettings.preset));
        rs.SetTargetInstanceCount(std::clamp(self->m_savedSettings.instanceCount, 100, 10000));
        rs.GetCullingSystem().enableFrustumCulling = self->m_savedSettings.enableFrustum;
        rs.GetCullingSystem().enableOcclusionCulling = self->m_savedSettings.enableOcclusion;
        rs.GetCullingSystem().depthBias = self->m_savedSettings.depthBias;
        if (self->m_socLabWindow) {
            self->m_socLabWindow->SetResolutionIndex(self->m_savedSettings.resolution);
            self->m_socLabWindow->ApplyResolution(rs);
        }
        rs.GetCullingSystem().visualMode = static_cast<VisualMode>(self->m_savedSettings.visualMode);
        rs.SetDepthPreviewFalseColor(self->m_savedSettings.depthFalseColor);
        rs.GetCamera().moveSpeed = self->m_savedSettings.moveSpeed;
        rs.RebuildScene();
    };
    iniHandler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
        auto* self = static_cast<ImGuiSystem*>(handler->UserData);
        if (!self || !self->m_renderSystem) return;
        auto& rs = *self->m_renderSystem;
        buf->append("[LabSettings][State]\n");
        buf->appendf("Preset=%d\n", static_cast<int>(rs.GetCurrentPreset()));
        buf->appendf("Instances=%d\n", rs.GetTargetInstanceCount());
        buf->appendf("FrustumCulling=%d\n", rs.GetCullingSystem().enableFrustumCulling ? 1 : 0);
        buf->appendf("OcclusionCulling=%d\n", rs.GetCullingSystem().enableOcclusionCulling ? 1 : 0);
        buf->appendf("DepthBias=%.6f\n", rs.GetCullingSystem().depthBias);
        const int resIdx = self->m_socLabWindow ? self->m_socLabWindow->GetResolutionIndex() : 2;
        buf->appendf("SOCResolution=%d\n", resIdx);
        buf->appendf("VisualMode=%d\n", static_cast<int>(rs.GetCullingSystem().visualMode));
        buf->appendf("DepthFalseColor=%d\n", rs.IsDepthPreviewFalseColor() ? 1 : 0);
        buf->appendf("MoveSpeed=%.2f\n", rs.GetCamera().moveSpeed);
        buf->append("\n");
    };
    ImGui::AddSettingsHandler(&iniHandler);

    ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    ImGui::StyleColorsDark();
    m_glfwInitialized = ImGui_ImplGlfw_InitForOther(m_window, true);
    if (!m_glfwInitialized) {
        Shutdown();
        return std::unexpected(elm::EngineError(elm::ErrorCode::UnknownError, "Failed to initialize ImGui GLFW backend"));
    }
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    auto& platformIO = ImGui::GetPlatformIO();
    platformIO.Renderer_CreateWindow = &ImGuiSystem::CreateViewport;
    platformIO.Renderer_DestroyWindow = &ImGuiSystem::DestroyViewport;
    platformIO.Renderer_SetWindowSize = &ImGuiSystem::ResizeViewport;
    platformIO.Renderer_RenderWindow = &ImGuiSystem::RenderViewport;
    platformIO.Renderer_SwapBuffers = &ImGuiSystem::PresentViewport;
    platformIO.Platform_GetWindowDpiScale = &ImGuiSystem::GetViewportDpiScale;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
    m_initialized = true;
    return {};
}

void ImGuiSystem::BeginFrame(RenderSystem& renderSystem) {
    if (!m_initialized) return;
    ImGui_ImplGlfw_NewFrame();
    const auto& swapChainDesc = renderSystem.GetSwapChain()->GetDesc();
    m_imGui->NewFrame(renderSystem.GetWidth(), renderSystem.GetHeight(), swapChainDesc.PreTransform);
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    m_imGui->SetRenderSurface(static_cast<Diligent::Uint32>((std::max)(framebufferWidth, 1)),
                              static_cast<Diligent::Uint32>((std::max)(framebufferHeight, 1)),
                              swapChainDesc.PreTransform);
}

void ImGuiSystem::CreateViewport(ImGuiViewport* viewport) {
    auto* system = static_cast<ImGuiSystem*>(ImGui::GetIO().UserData);
    auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
    if (!system || !system->m_renderSystem || !window) return;

    Diligent::SwapChainDesc description = system->m_renderSystem->GetSwapChain()->GetDesc();
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    description.Width = static_cast<Diligent::Uint32>((std::max)(framebufferWidth, 1));
    description.Height = static_cast<Diligent::Uint32>((std::max)(framebufferHeight, 1));
    description.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;

    Diligent::ISwapChain* swapChain = nullptr;
#if PLATFORM_WIN32
    auto* factory = Diligent::GetEngineFactoryD3D12();
    Diligent::Win32NativeWindow nativeWindow{glfwGetWin32Window(window)};
    factory->CreateSwapChainD3D12(system->m_renderSystem->GetRenderDevice(),
                                  system->m_renderSystem->GetDeviceContext(), description,
                                  Diligent::FullScreenModeDesc{}, nativeWindow, &swapChain);
#else
    auto* factory = Diligent::GetEngineFactoryVk();
    Diligent::LinuxNativeWindow nativeWindow;
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        nativeWindow.pDisplay = glfwGetWaylandDisplay();
        nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(glfwGetWaylandWindow(window)));
    } else {
        nativeWindow.pDisplay = glfwGetX11Display();
        nativeWindow.WindowId = static_cast<uint32_t>(glfwGetX11Window(window));
    }
    factory->CreateSwapChainVk(system->m_renderSystem->GetRenderDevice(),
                                system->m_renderSystem->GetDeviceContext(), description,
                                nativeWindow, &swapChain);
#endif
    if (swapChain) {
        auto* data = new ImGuiViewportRendererData;
        data->swapChain = swapChain;
        viewport->RendererUserData = data;
    }
}

void ImGuiSystem::DestroyViewport(ImGuiViewport* viewport) {
    delete static_cast<ImGuiViewportRendererData*>(viewport->RendererUserData);
    viewport->RendererUserData = nullptr;
}

void ImGuiSystem::ResizeViewport(ImGuiViewport* viewport, ImVec2) {
    auto* data = static_cast<ImGuiViewportRendererData*>(viewport->RendererUserData);
    if (!data || !data->swapChain) return;

    auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
    if (!window) return;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    data->swapChain->Resize(static_cast<Diligent::Uint32>((std::max)(framebufferWidth, 1)),
                            static_cast<Diligent::Uint32>((std::max)(framebufferHeight, 1)));
}

float ImGuiSystem::GetViewportDpiScale(ImGuiViewport* viewport) {
    auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
    if (!window) return 1.0f;

    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    return xScale > 0.0f ? xScale : 1.0f;
}

void ImGuiSystem::RenderViewport(ImGuiViewport* viewport, void* userData) {
    auto* system = static_cast<ImGuiSystem*>(userData);
    auto* data = static_cast<ImGuiViewportRendererData*>(viewport->RendererUserData);
    if (!system || !data || !data->swapChain) return;

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(static_cast<GLFWwindow*>(viewport->PlatformHandle), &windowWidth, &windowHeight);
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(viewport->PlatformHandle), &framebufferWidth, &framebufferHeight);
    if (windowWidth > 0 && windowHeight > 0) {
        viewport->DrawData->FramebufferScale = ImVec2(
            static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth),
            static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight));
    }
    system->m_imGui->SetRenderSurface(static_cast<Diligent::Uint32>((std::max)(framebufferWidth, 1)),
                                       static_cast<Diligent::Uint32>((std::max)(framebufferHeight, 1)),
                                       data->swapChain->GetDesc().PreTransform);
    auto* renderTarget = data->swapChain->GetCurrentBackBufferRTV();
    const float clearColor[] = {0.11f, 0.13f, 0.16f, 1.0f};
    system->m_renderSystem->GetDeviceContext()->SetRenderTargets(
        1, &renderTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    system->m_renderSystem->GetDeviceContext()->ClearRenderTarget(
        renderTarget, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    system->m_imGui->RenderDrawData(system->m_renderSystem->GetDeviceContext(), viewport->DrawData);
}

void ImGuiSystem::PresentViewport(ImGuiViewport* viewport, void*) {
    auto* data = static_cast<ImGuiViewportRendererData*>(viewport->RendererUserData);
    if (data && data->swapChain) data->swapChain->Present();
}

void ImGuiSystem::Render(RenderSystem& renderSystem, const FrameStats& stats) {
    if (!m_initialized) return;

    m_dockSpace.Render(m_windows);
    for (const auto& window : m_windows) {
        if (window && window->IsVisible()) {
            window->Render(renderSystem, stats);
        }
    }

    auto* context = renderSystem.GetDeviceContext();
    auto* swapChain = renderSystem.GetSwapChain();
    auto* rtv = swapChain->GetCurrentBackBufferRTV();
    context->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_imGui->Render(context);
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault(this, this);
}

void ImGuiSystem::Shutdown() {
    if (m_initialized) {
        const auto& io = ImGui::GetIO();
        if (io.IniFilename != nullptr) {
            ImGui::SaveIniSettingsToDisk(io.IniFilename);
        }
    }
    if (m_glfwInitialized) {
        ImGui_ImplGlfw_Shutdown();
        m_glfwInitialized = false;
    }
    if (m_imGui) ImGui::DestroyPlatformWindows();
    m_imGui.reset();
    m_windows.clear();
    m_socLabWindow = nullptr;
    m_renderSystem = nullptr;
    m_initialized = false;
}

} // namespace Engine
