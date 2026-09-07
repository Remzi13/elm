#pragma once

#include "core/Error.hpp"

#include <memory>

struct GLFWwindow;
struct ImGuiViewport;
struct ImVec2;

namespace Diligent {
	class IRenderDevice;
	class IDeviceContext;
	class ImGuiImplDiligentViewport;
}

namespace elm {
	class RenderSystem;

struct ImGuiRenderContext {
	RenderSystem* renderSystem{ nullptr };
	GLFWwindow* window{ nullptr };
	uint32_t width{ 0 };
	uint32_t height{ 0 };
};

class ImGuiRenderer {
public:
	ImGuiRenderer();
	~ImGuiRenderer();

	ImGuiRenderer(const ImGuiRenderer&) = delete;
	ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

	[[nodiscard]] auto Init(const ImGuiRenderContext& context) -> EngineResult<void>;
	void BeginFrame();
	void Render();
	void Shutdown();

private:
	static void CreateViewport(ImGuiViewport* viewport);
	static void DestroyViewport(ImGuiViewport* viewport);
	static void ResizeViewport(ImGuiViewport* viewport, ImVec2 size);
	static float GetViewportDpiScale(ImGuiViewport* viewport);
	static void RenderViewport(ImGuiViewport* viewport, void* userData);
	static void PresentViewport(ImGuiViewport* viewport, void* userData);
	static void DrawViewport(void* userData, void* deviceContext, void* drawData);

	GLFWwindow* m_window{ nullptr };
	ImGuiRenderContext m_context;
	std::unique_ptr<Diligent::ImGuiImplDiligentViewport> m_backend;
	bool m_initialized{ false };
	bool m_glfwInitialized{ false };
};

} // namespace elm
