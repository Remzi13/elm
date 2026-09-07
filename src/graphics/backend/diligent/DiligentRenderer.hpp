#pragma once

#include "core/Error.hpp"

#include "graphics/VisibilitySystem.hpp"
#include "graphics/Camera.hpp"
#include "graphics/RenderResourceManager.hpp"
#include "graphics/RenderSurfaceManager.hpp"

#include "Scene/TestScenes.hpp"

#include <memory>

struct GLFWwindow;

namespace elm {

	class ImGuiRenderer;

	struct GpuInstanceData {
		Matrix4x4 World;
		Vector4 Color;
	};

	class DiligentRenderer : public RenderSurfaceManager {
		friend class ImGuiRenderer;
	public:
		using ViewportDrawCallback = void(*)(void* userData, void* deviceContext, void* drawData);
		DiligentRenderer();
		~DiligentRenderer();

		DiligentRenderer(const DiligentRenderer&) = delete;
		DiligentRenderer& operator=(const DiligentRenderer&) = delete;
		DiligentRenderer(DiligentRenderer&&) noexcept = delete;
		DiligentRenderer& operator=(DiligentRenderer&&) noexcept = delete;

		[[nodiscard]] auto Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void>;
		[[nodiscard]] bool ShouldClose() const;		
				
		void BeginFrame();
		void RenderScene(const Camera& camera, const Scene& scene);
		void EndFrame();
		void Shutdown();

		[[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }				
		[[nodiscard]] OcclusionCullingSystem& GetCullingSystem() { return m_visibilitySystem.GetSoftwareOcclusionCuller(); }
		[[nodiscard]] RenderViewport* CreateViewport(GLFWwindow* window, uint32_t width, uint32_t height) override;
		void DestroyViewport(RenderViewport* viewport) override;
		void ResizeViewport(RenderViewport* viewport, uint32_t width, uint32_t height) override;
		void RenderViewportFrame(RenderViewport* viewport, void* drawData);
		void PresentViewportFrame(RenderViewport* viewport);
		void SetViewportDrawCallback(ViewportDrawCallback callback, void* userData);
		[[nodiscard]] RenderTexture* GetEngineViewportTexture() const { return m_engineViewportTexture.get(); }
		[[nodiscard]] uint32_t GetEngineViewportWidth() const { return m_engineViewportWidth; }
		[[nodiscard]] uint32_t GetEngineViewportHeight() const { return m_engineViewportHeight; }
		[[nodiscard]] uint32_t GetWidth() const { return m_width; }
		[[nodiscard]] uint32_t GetHeight() const { return m_height; }
		[[nodiscard]] size_t GetMemAllocated() const;				
		[[nodiscard]] bool IsDepthPreviewFalseColor() const { return m_depthPreviewFalseColor; }
		void SetDepthPreviewFalseColor(bool falseColor) { m_depthPreviewFalseColor = falseColor; }
		[[nodiscard]] RenderTexture* GetDepthPreviewTexture() const { return m_depthPreviewTexture.get(); }
		[[nodiscard]] uint32_t GetDepthPreviewWidth() const { return m_depthPreviewWidth; }
		[[nodiscard]] uint32_t GetDepthPreviewHeight() const { return m_depthPreviewHeight; }
		void CreateDepthPreviewTexture(uint32_t width, uint32_t height);
		[[nodiscard]] void* GetNativeDevice() const noexcept;
		[[nodiscard]] void* GetNativeContext() const noexcept;
		[[nodiscard]] void* GetNativeSwapChain() const noexcept;
		
	private:
		struct Impl;
		struct ViewportRegistry;

		void InitPipeline();
		void CreateMeshBuffers();
		void CreateInstanceBuffer();
		void CreateEngineViewport(uint32_t width, uint32_t height);
		void UpdateDepthPreviewTexture();

	private:
		GLFWwindow* m_window{ nullptr };

		uint32_t m_cubeIndexCount{ 0 };
		uint32_t m_wallIndexCount{ 0 };
		uint32_t m_groundIndexCount{ 0 };
		static constexpr size_t MaxInstances = 30000;

		// Depth Buffer Visualization Texture
		uint32_t m_depthPreviewWidth{ 256 };
		uint32_t m_depthPreviewHeight{ 144 };
		Vector<uint32_t> m_depthPreviewPixels;
		bool m_depthPreviewFalseColor{ true };

		// Offscreen render target displayed inside the dockspace.
		uint32_t m_engineViewportWidth{ 1280 };
		uint32_t m_engineViewportHeight{ 720 };
		bool m_engineViewportIsShaderResource{ false };
		
		VisibilitySystem m_visibilitySystem;
				
		Vector<GpuInstanceData> m_visibleGpuInstances;
		Vector<GpuInstanceData> m_culledGpuInstances;

		uint32_t m_width{ 1280 };
		uint32_t m_height{ 720 };
		bool m_initialized{ false };
		std::unique_ptr<Impl> m_impl;
		std::unique_ptr<ViewportRegistry> m_viewports;
		std::unique_ptr<RenderTexture> m_engineViewportTexture;
		std::unique_ptr<RenderTexture> m_depthPreviewTexture;
	};

} // namespace Engine
