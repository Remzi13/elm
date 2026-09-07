#include "graphics/ImGuiRenderer.hpp"

#include "graphics/RenderSystem.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

#include "ImGuiDiligentRenderer.hpp"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"
#include "imgui.h"

namespace Diligent {
class ImGuiImplDiligentViewport final : public ImGuiImplDiligent {
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

namespace elm {
namespace {
struct ViewportData {
	RenderViewport* viewport{ nullptr };
};
}

ImGuiRenderer::~ImGuiRenderer() {
	Shutdown();
}

ImGuiRenderer::ImGuiRenderer() = default;

auto ImGuiRenderer::Init(const ImGuiRenderContext& context) -> EngineResult<void> {
	m_context = context;
	m_window = context.window;

	auto* swapChain = static_cast<Diligent::ISwapChain*>(context.renderSystem->GetNativeSwapChain());
	const auto& swapChainDesc = swapChain->GetDesc();
	Diligent::ImGuiDiligentCreateInfo createInfo;
	createInfo.pDevice = static_cast<Diligent::IRenderDevice*>(context.renderSystem->GetNativeDevice());
	createInfo.BackBufferFmt = swapChainDesc.ColorBufferFormat;
	createInfo.DepthBufferFmt = Diligent::TEX_FORMAT_UNKNOWN;
	m_backend = std::make_unique<Diligent::ImGuiImplDiligentViewport>(createInfo);

	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable |
		ImGuiConfigFlags_DpiEnableScaleViewports | ImGuiConfigFlags_DpiEnableScaleFonts;
	io.ConfigViewportsNoAutoMerge = false;
	io.UserData = this;
	context.renderSystem->SetViewportDrawCallback(&ImGuiRenderer::DrawViewport, this);
	ImGui::StyleColorsDark();

	m_glfwInitialized = ImGui_ImplGlfw_InitForOther(m_window, true);
	if (!m_glfwInitialized) {
		Shutdown();
		return std::unexpected(EngineError(ErrorCode::UnknownError, "Failed to initialize ImGui GLFW backend"));
	}

	auto& platformIO = ImGui::GetPlatformIO();
	platformIO.Renderer_CreateWindow = &ImGuiRenderer::CreateViewport;
	platformIO.Renderer_DestroyWindow = &ImGuiRenderer::DestroyViewport;
	platformIO.Renderer_SetWindowSize = &ImGuiRenderer::ResizeViewport;
	platformIO.Renderer_RenderWindow = &ImGuiRenderer::RenderViewport;
	platformIO.Renderer_SwapBuffers = &ImGuiRenderer::PresentViewport;
	platformIO.Platform_GetWindowDpiScale = &ImGuiRenderer::GetViewportDpiScale;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
	m_initialized = true;
	return {};
}

void ImGuiRenderer::BeginFrame() {
	if (!m_initialized) return;
	ImGui_ImplGlfw_NewFrame();
	auto* swapChain = static_cast<Diligent::ISwapChain*>(m_context.renderSystem->GetNativeSwapChain());
	m_backend->NewFrame(m_context.width, m_context.height, swapChain->GetDesc().PreTransform);
	int framebufferWidth = 0;
	int framebufferHeight = 0;
	glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
	m_backend->SetRenderSurface(
		static_cast<Diligent::Uint32>((std::max)(framebufferWidth, 1)),
		static_cast<Diligent::Uint32>((std::max)(framebufferHeight, 1)),
		swapChain->GetDesc().PreTransform);
}

void ImGuiRenderer::CreateViewport(ImGuiViewport* viewport) {
	auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData);
	auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
	if (!renderer || !renderer->m_context.renderSystem || !window) return;

	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);

	if (!renderer->m_context.renderSystem) return;
	auto* renderViewport = renderer->m_context.renderSystem->CreateViewport(window,
		static_cast<uint32_t>((std::max)(width, 1)), static_cast<uint32_t>((std::max)(height, 1)));
	if (renderViewport) {
		auto* data = new ViewportData;
		data->viewport = renderViewport;
		viewport->RendererUserData = data;
	}
}

void ImGuiRenderer::DestroyViewport(ImGuiViewport* viewport) {
	if (auto* data = static_cast<ViewportData*>(viewport->RendererUserData)) {
		if (auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData)) {
			renderer->m_context.renderSystem->DestroyViewport(data->viewport);
		}
		delete data;
	}
	viewport->RendererUserData = nullptr;
}

void ImGuiRenderer::ResizeViewport(ImGuiViewport* viewport, ImVec2) {
	auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
	if (!data) return;
	auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
	if (!window) return;
	int width = 0;
	int height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	if (auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData)) {
		renderer->m_context.renderSystem->ResizeViewport(data->viewport,
			static_cast<uint32_t>((std::max)(width, 1)), static_cast<uint32_t>((std::max)(height, 1)));
	}
}

float ImGuiRenderer::GetViewportDpiScale(ImGuiViewport* viewport) {
	auto* window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
	if (!window) return 1.0f;
	float xScale = 1.0f;
	float yScale = 1.0f;
	glfwGetWindowContentScale(window, &xScale, &yScale);
	return xScale > 0.0f ? xScale : 1.0f;
}

void ImGuiRenderer::RenderViewport(ImGuiViewport* viewport, void*) {
	auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData);
	auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
	if (!renderer || !data || !renderer->m_context.renderSystem) return;

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
	renderer->m_context.renderSystem->RenderViewportFrame(data->viewport, viewport->DrawData);
}

void ImGuiRenderer::PresentViewport(ImGuiViewport* viewport, void*) {
	auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
	if (data) {
		if (auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData)) {
			renderer->m_context.renderSystem->PresentViewportFrame(data->viewport);
		}
	}
}

void ImGuiRenderer::DrawViewport(void*, void* deviceContext, void* drawData) {
	auto* context = static_cast<Diligent::IDeviceContext*>(deviceContext);
	auto* data = static_cast<ImDrawData*>(drawData);
	if (auto* renderer = static_cast<ImGuiRenderer*>(ImGui::GetIO().UserData)) {
		renderer->m_backend->RenderDrawData(context, data);
	}
}

void ImGuiRenderer::Render() {
	if (!m_initialized) return;
	auto* swapChain = static_cast<Diligent::ISwapChain*>(m_context.renderSystem->GetNativeSwapChain());
	auto* deviceContext = static_cast<Diligent::IDeviceContext*>(m_context.renderSystem->GetNativeContext());
	auto* renderTarget = swapChain->GetCurrentBackBufferRTV();
	deviceContext->SetRenderTargets(1, &renderTarget, nullptr,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	m_backend->Render(deviceContext);
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault(this, this);
}

void ImGuiRenderer::Shutdown() {
	if (m_backend) ImGui::DestroyPlatformWindows();
	if (m_context.renderSystem) {
		m_context.renderSystem->SetViewportDrawCallback(nullptr, nullptr);
	}
	if (m_glfwInitialized) {
		ImGui_ImplGlfw_Shutdown();
		m_glfwInitialized = false;
	}
	m_backend.reset();
	m_context = {};
	m_window = nullptr;
	m_initialized = false;
}

} // namespace elm
