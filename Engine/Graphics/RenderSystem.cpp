#include "Engine/Graphics/RenderSystem.hpp"

#include <GLFW/glfw3.h>

// Diligent Engine Includes
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#include "Graphics/GraphicsTools/interface/CommonlyUsedStates.h"

// ImGui integration
#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"

#include <iostream>
#include <vector>

#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

namespace Engine {

RenderSystem::RenderSystem() = default;

RenderSystem::~RenderSystem() {
    Shutdown();
}

auto RenderSystem::Init(uint32_t width, uint32_t height, std::string_view title) -> EngineResult<void> {
    if (m_initialized) {
        return {};
    }

    m_width = width;
    m_height = height;

    // Initialize GLFW
    if (!glfwInit()) {
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to initialize GLFW"));
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Disable OpenGL context for Vulkan/Native Diligent
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.data(), nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to create GLFW window"));
    }

    std::cout << "[RenderSystem] GLFW window created successfully (" << width << "x" << height << ")." << std::endl;

    // Initialize Diligent Engine Vulkan Factory
    #if EXPLICITLY_LOAD_ENGINE_VK_DLL
        auto GetEngineFactoryVk = Diligent::LoadGraphicsEngineVk();
    #endif

    auto* pFactoryVk = Diligent::GetEngineFactoryVk();
    if (!pFactoryVk) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryVk"));
    }

    Diligent::EngineVkCreateInfo engineCreateInfo;
    
    // Create Render Device and Contexts
    engineCreateInfo.NumDeferredContexts = 0;
    pFactoryVk->CreateDeviceAndContextsVk(engineCreateInfo, &m_renderDevice, &m_deviceContext);

    if (!m_renderDevice || !m_deviceContext) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent Render Device & Contexts"));
    }

    // m_deviceContext is already set

    // Create SwapChain
    Diligent::SwapChainDesc swapChainDesc;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;

    Diligent::LinuxNativeWindow nativeWindow;
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        nativeWindow.pDisplay = glfwGetWaylandDisplay();
        nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(glfwGetWaylandWindow(m_window)));
    } else {
        nativeWindow.pDisplay = glfwGetX11Display();
        nativeWindow.WindowId = static_cast<uint32_t>(glfwGetX11Window(m_window));
    }

    pFactoryVk->CreateSwapChainVk(m_renderDevice, m_deviceContext, swapChainDesc, nativeWindow, &m_swapChain);

    if (!m_swapChain) {
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent SwapChain"));
    }

    // Initialize ImGui Diligent Backend
    const auto& SCDesc = m_swapChain->GetDesc();
    Diligent::ImGuiDiligentCreateInfo ImGuiCI;
    ImGuiCI.pDevice = m_renderDevice;
    ImGuiCI.BackBufferFmt = SCDesc.ColorBufferFormat;
    ImGuiCI.DepthBufferFmt = SCDesc.DepthBufferFormat;
    
    m_imGui = std::make_unique<Diligent::ImGuiImplDiligent>(ImGuiCI);

    // Setup ImGui GLFW bindings
    ImGui_ImplGlfw_InitForOther(m_window, true);

    m_initialized = true;
    std::cout << "[RenderSystem] Diligent Engine & ImGui backend initialized successfully." << std::endl;

    return {};
}

bool RenderSystem::ShouldClose() const {
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

void RenderSystem::PollEvents() {
    glfwPollEvents();
}

void RenderSystem::BeginFrame() {
    if (!m_swapChain || !m_deviceContext) return;

    auto* pRTV = m_swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_swapChain->GetDepthBufferDSV();

    // Clear back buffer and depth buffer
    const float clearColor[] = { 0.12f, 0.15f, 0.18f, 1.0f };
    m_deviceContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_deviceContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // New Frame for ImGui
    ImGui_ImplGlfw_NewFrame();
    m_imGui->NewFrame(m_width, m_height, m_swapChain->GetDesc().PreTransform);
}

void RenderSystem::RenderUI(const FrameStats& stats) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 260), ImGuiCond_FirstUseEver);

    ImGui::Begin("Engine Statistics & Physics Diagnostics");

    ImGui::Text("Graphics: Diligent Engine (Vulkan)");
    ImGui::Text("Physics: Jolt Physics Engine");
    ImGui::Separator();

    ImGui::Text("FPS: %.1f", stats.fps);
    ImGui::Text("Frame Time: %.3f ms", stats.deltaTimeMs);
    ImGui::Separator();

    ImGui::Text("Active Physics Bodies: %u", stats.physicsBodyCount);
    
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Dynamic Box Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", stats.boxTransform.position.x, stats.boxTransform.position.y, stats.boxTransform.position.z);
        ImGui::Text("Rot: (%.2f, %.2f, %.2f, %.2f)", stats.boxTransform.rotation.x, stats.boxTransform.rotation.y, stats.boxTransform.rotation.z, stats.boxTransform.rotation.w);
    }

    if (ImGui::CollapsingHeader("Static Ground Transform")) {
        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", stats.groundTransform.position.x, stats.groundTransform.position.y, stats.groundTransform.position.z);
    }

    ImGui::End();

    ImGui::Render();
}

void RenderSystem::EndFrame() {
    if (!m_swapChain || !m_deviceContext) return;

    // Render ImGui draw commands using Diligent backend
    if (m_imGui) {
        m_imGui->Render(m_deviceContext);
    }

    // Present swapchain
    m_swapChain->Present();
}

void RenderSystem::Shutdown() {
    if (!m_initialized) return;

    m_imGui.reset();
    ImGui_ImplGlfw_Shutdown();

    if (m_swapChain) {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    if (m_deviceContext) {
        m_deviceContext->Release();
        m_deviceContext = nullptr;
    }
    if (m_renderDevice) {
        m_renderDevice->Release();
        m_renderDevice = nullptr;
    }

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();

    m_initialized = false;
    std::cout << "[RenderSystem] Shutdown completed." << std::endl;
}

} // namespace Engine
