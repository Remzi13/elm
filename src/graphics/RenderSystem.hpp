#pragma once

#include "core/Error.hpp"

#include "graphics/Camera.hpp"

#include "Scene/Transform.hpp"
#include "Scene/TestScenes.hpp"

#include "graphics/render/BufferManager.hpp"
#include "graphics/render/TextureManager.hpp"
#include "graphics/render/DynamicLinearAllocator.hpp"
#include "graphics/Settings.hpp"

struct GLFWwindow;

namespace Diligent {
	struct IRenderDevice;
	struct IDeviceContext;
	class ISwapChain;
	class IPipelineState;
	class IShaderResourceBinding;	
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

	struct FrameData {		
		Camera camera;
		Scene scene;
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
		void Draw(FrameData& frameData, Settings& settings);
		void EndFrame();
		void Shutdown();

		[[nodiscard]] GLFWwindow* GetWindowHandle() const { return m_window; }
		[[nodiscard]] Diligent::IRenderDevice* GetRenderDevice() const { return m_renderDevice; }
		[[nodiscard]] Diligent::IDeviceContext* GetDeviceContext() const { return m_deviceContext; }
		[[nodiscard]] Diligent::ISwapChain* GetSwapChain() const { return m_swapChain; }
		[[nodiscard]] Diligent::ITextureView* GetEngineViewportSRV() const { return m_pEngineViewportSRV; }
		[[nodiscard]] uint32_t GetEngineViewportWidth() const { return m_engineViewportWidth; }
		[[nodiscard]] uint32_t GetEngineViewportHeight() const { return m_engineViewportHeight; }
		[[nodiscard]] uint32_t GetWidth() const { return m_width; }
		[[nodiscard]] uint32_t GetHeight() const { return m_height; }
		[[nodiscard]] size_t GetMemAllocated() const;
		[[nodiscard]] render::TextureManager& GetTextureManager() noexcept { return m_textureManager; }
		[[nodiscard]] const render::TextureManager& GetTextureManager() const noexcept { return m_textureManager; }
		[[nodiscard]] Diligent::ITextureView* GetTextureSRV(const render::Texture& texture) const { return m_textureManager.getTextureSRV(texture); }

	private:
		struct Mesh {
			render::BufferHandler vb;
			render::BufferHandler ib;
			uint32_t indexCount{ 0 };
		};

	private:
		void InitPipeline();
		void CreateMeshBuffers();
		void CreateEngineViewport(uint32_t width, uint32_t height);

		void Draw(const Mesh& mesh, const Vector<GpuInstanceData>& instances);

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

		Mesh m_cube;
		Mesh m_wall;
		Mesh m_ground;

		// Dynamic Instance Buffer
		static constexpr size_t MaxInstances = 30000;

		// Offscreen render target displayed inside the dockspace.
		Diligent::ITexture* m_pEngineViewportTex{ nullptr };
		Diligent::ITextureView* m_pEngineViewportRTV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportDSV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportSRV{ nullptr };
		uint32_t m_engineViewportWidth{ 1280 };
		uint32_t m_engineViewportHeight{ 720 };
		bool m_engineViewportIsShaderResource{ false };
				
				
		Vector<GpuInstanceData> m_visibleGpuInstances;
		Vector<GpuInstanceData> m_culledGpuInstances;

		render::BufferManager m_bufferManager;
		render::TextureManager m_textureManager;
		render::DynamicLinearAllocator m_dynamicInstanceBuffer;
		render::DynamicLinearAllocator m_dynamicUniformBuffer;

		uint32_t m_width{ 1280 };
		uint32_t m_height{ 720 };
		bool m_initialized{ false };
	};

} // namespace Engine
