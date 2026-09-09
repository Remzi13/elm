#pragma once

#include "core/Error.hpp"

#include "graphics/culling/OcclusionCullingSystem.hpp"
#include "graphics/Camera.hpp"

#include "Scene/Transform.hpp"
#include "Scene/TestScenes.hpp"

#include "graphics/render/BufferManager.hpp"
#include "graphics/render/DynamicLinearAllocator.hpp"

struct GLFWwindow;

namespace Diligent {
	struct IRenderDevice;
	struct IDeviceContext;
	class ISwapChain;
	class IPipelineState;
	class IShaderResourceBinding;
	//struct IBuffer;
	class ITexture;
	class ITextureView;
}

namespace elm {

	struct FrameStats {
		float fps{ 0.0f };
		float deltaTimeMs{ 0.0f };
		uint32_t physicsBodyCount{ 0 };
		Transform boxTransform;
		Transform groundTransform;
	};

	struct GpuInstanceData {
		Matrix4x4 World;
		Vector4 Color;
	};

	class RenderSystem {
		friend class ImGuiSystem;
	public:
		RenderSystem();
		~RenderSystem();

		RenderSystem(const RenderSystem&) = delete;
		RenderSystem& operator=(const RenderSystem&) = delete;
		RenderSystem(RenderSystem&&) noexcept = delete;
		RenderSystem& operator=(RenderSystem&&) noexcept = delete;

		[[nodiscard]] auto Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void>;
		[[nodiscard]] bool ShouldClose() const;		
				
		void BeginFrame();
		void RenderScene(const Camera& camera, const Scene& scene);
		void EndFrame();
		void Shutdown();

		[[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }				
		[[nodiscard]] OcclusionCullingSystem& GetCullingSystem() { return m_cullingSystem; }
		[[nodiscard]] Diligent::IRenderDevice* GetRenderDevice() const { return m_renderDevice; }
		[[nodiscard]] Diligent::IDeviceContext* GetDeviceContext() const { return m_deviceContext; }
		[[nodiscard]] Diligent::ISwapChain* GetSwapChain() const { return m_swapChain; }
		[[nodiscard]] Diligent::ITextureView* GetEngineViewportSRV() const { return m_pEngineViewportSRV; }
		[[nodiscard]] uint32_t GetEngineViewportWidth() const { return m_engineViewportWidth; }
		[[nodiscard]] uint32_t GetEngineViewportHeight() const { return m_engineViewportHeight; }
		[[nodiscard]] uint32_t GetWidth() const { return m_width; }
		[[nodiscard]] uint32_t GetHeight() const { return m_height; }
		[[nodiscard]] size_t GetMemAllocated() const;				
		[[nodiscard]] bool IsDepthPreviewFalseColor() const { return m_depthPreviewFalseColor; }
		void SetDepthPreviewFalseColor(bool falseColor) { m_depthPreviewFalseColor = falseColor; }
		[[nodiscard]] Diligent::ITextureView* GetDepthPreviewSRV() const { return m_pDepthPreviewSRV; }
		[[nodiscard]] uint32_t GetDepthPreviewWidth() const { return m_depthPreviewWidth; }
		[[nodiscard]] uint32_t GetDepthPreviewHeight() const { return m_depthPreviewHeight; }
		void CreateDepthPreviewTexture(uint32_t width, uint32_t height);
		
	private:
		void InitPipeline();
		void CreateMeshBuffers();		
		void CreateEngineViewport(uint32_t width, uint32_t height);
		void UpdateDepthPreviewTexture();

	private:
		GLFWwindow* m_window{ nullptr };
		
		// Diligent Engine components
		Diligent::IRenderDevice* m_renderDevice{ nullptr };
		Diligent::IDeviceContext* m_deviceContext{ nullptr };
		Diligent::ISwapChain* m_swapChain{ nullptr };

		// Shaders & Pipelines
		Diligent::IPipelineState* m_pPSO{ nullptr };
		Diligent::IPipelineState* m_pHighlightPSO{ nullptr };
		Diligent::IShaderResourceBinding* m_pSRB{ nullptr };
		Diligent::IBuffer* m_pCameraConstantsBuffer{ nullptr };

		// Geometry Buffers
		render::BufferHandler m_pCubeVB;
		render::BufferHandler m_pCubeIB;
		uint32_t m_cubeIndexCount{ 0 };

		render::BufferHandler m_pWallVB;
		render::BufferHandler m_pWallIB;
		uint32_t m_wallIndexCount{ 0 };

		render::BufferHandler m_pGroundVB;
		render::BufferHandler m_pGroundIB;
		uint32_t m_groundIndexCount{ 0 };

		// Dynamic Instance Buffer
		static constexpr size_t MaxInstances = 30000;

		// Depth Buffer Visualization Texture
		Diligent::ITexture* m_pDepthPreviewTex{ nullptr };
		Diligent::ITextureView* m_pDepthPreviewSRV{ nullptr };
		uint32_t m_depthPreviewWidth{ 256 };
		uint32_t m_depthPreviewHeight{ 144 };
		Vector<uint32_t> m_depthPreviewPixels;
		bool m_depthPreviewFalseColor{ true };

		// Offscreen render target displayed inside the dockspace.
		Diligent::ITexture* m_pEngineViewportTex{ nullptr };
		Diligent::ITextureView* m_pEngineViewportRTV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportDSV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportSRV{ nullptr };
		uint32_t m_engineViewportWidth{ 1280 };
		uint32_t m_engineViewportHeight{ 720 };
		bool m_engineViewportIsShaderResource{ false };
		
		OcclusionCullingSystem m_cullingSystem;
				
		Vector<GpuInstanceData> m_visibleGpuInstances;
		Vector<GpuInstanceData> m_culledGpuInstances;

		render::BufferManager m_bufferManager;
		render::DynamicLinearAllocator m_dynamicLinearAllocator;

		uint32_t m_width{ 1280 };
		uint32_t m_height{ 720 };
		bool m_initialized{ false };
	};

} // namespace Engine
