#include "graphics/backend/diligent/DiligentRenderer.hpp"

#include "core/Log.hpp"

#include <GLFW/glfw3.h>

// Diligent Engine Includes
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsTools/interface/CommonlyUsedStates.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <iostream>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <unordered_map>

#if PLATFORM_WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

namespace elm {

	struct DiligentRenderer::Impl {
		Diligent::IRenderDevice* m_renderDevice{ nullptr };
		Diligent::IDeviceContext* m_deviceContext{ nullptr };
		Diligent::ISwapChain* m_swapChain{ nullptr };
		Diligent::IPipelineState* m_pPSO{ nullptr };
		Diligent::IPipelineState* m_pHighlightPSO{ nullptr };
		Diligent::IShaderResourceBinding* m_pSRB{ nullptr };
		Diligent::IBuffer* m_pCameraConstantsBuffer{ nullptr };
		Diligent::IBuffer* m_pCubeVB{ nullptr };
		Diligent::IBuffer* m_pCubeIB{ nullptr };
		Diligent::IBuffer* m_pWallVB{ nullptr };
		Diligent::IBuffer* m_pWallIB{ nullptr };
		Diligent::IBuffer* m_pGroundVB{ nullptr };
		Diligent::IBuffer* m_pGroundIB{ nullptr };
		Diligent::IBuffer* m_pInstanceBuffer{ nullptr };
		Diligent::ITexture* m_pDepthPreviewTex{ nullptr };
		Diligent::ITextureView* m_pDepthPreviewSRV{ nullptr };
		Diligent::ITexture* m_pEngineViewportTex{ nullptr };
		Diligent::ITextureView* m_pEngineViewportRTV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportDSV{ nullptr };
		Diligent::ITextureView* m_pEngineViewportSRV{ nullptr };
	};


	struct DiligentRenderer::ViewportRegistry {
		struct Entry {
			Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;
			RenderViewport viewport;
		};

		std::unordered_map<uint64_t, Entry> viewports;
		uint64_t nextId{ 1 };
		ViewportDrawCallback drawCallback{ nullptr };
		void* drawUserData{ nullptr };
	};

	namespace {
		class DilligentAllocator : public Diligent::IMemoryAllocator
		{
		public:
			virtual ~DilligentAllocator() = default;

			struct AllocationHeader
			{
				void* rawPointer;
				size_t requestedSize;
				size_t allocatedSize;
			};

			virtual void* Allocate(size_t Size, [[maybe_unused]] const char* DebugDesc, [[maybe_unused]] const char* File, [[maybe_unused]] int Line) override
			{
				constexpr size_t Alignment = 64;
				size_t allocatedSize = Size + Alignment + sizeof(AllocationHeader);
				void* rawPointer = memory::allocate_impl(allocatedSize);
				void* ptr = static_cast<char*>(rawPointer) + sizeof(AllocationHeader);
				size_t availableSize = allocatedSize - sizeof(AllocationHeader);
				std::align(Alignment, Size, ptr, availableSize);
				if (!ptr) return nullptr;

				auto* header = reinterpret_cast<AllocationHeader*>(ptr) - 1;
				header->rawPointer = rawPointer;
				header->requestedSize = Size;
				header->allocatedSize = allocatedSize;
				m_TotalAllocated.fetch_add(Size, std::memory_order_relaxed);

				return ptr;
			}

			virtual void Free(void* Ptr) override
			{
				if (!Ptr) return;

				auto* header = reinterpret_cast<AllocationHeader*>(Ptr) - 1;
				void* rawPointer = header->rawPointer;
				const size_t requestedSize = header->requestedSize;
				const size_t allocatedSize = header->allocatedSize;

				m_TotalAllocated.fetch_sub(requestedSize, std::memory_order_relaxed);
				memory::deallocate_impl(rawPointer, allocatedSize);

			}

			size_t GetTotalAllocatedBytes() const
			{
				return m_TotalAllocated.load(std::memory_order_relaxed);
			}

		private:
			std::atomic<size_t> m_TotalAllocated{ 0 };
		} g_Allocator;
	}

	// Shaders are loaded from shaders/ at runtime. This keeps shader editing independent
	// from the executable and also allows the same source to be replaced without a rebuild.
	static String LoadShaderSource(const char* fileName) {
		const std::filesystem::path sourceRoot =
			std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
		const std::filesystem::path paths[] = {
			std::filesystem::current_path() / "shaders" / fileName,
			std::filesystem::current_path() / "assets/shaders" / fileName,
			sourceRoot / "shaders" / fileName,
			sourceRoot / "assets/shaders" / fileName
		};
		for (const auto& path : paths) {
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file) continue;

			const auto size = file.tellg();
			if (size <= 0 || !file.seekg(0)) continue;

			String source(static_cast<size_t>(size), '\0');
			if (file.read(source.data(), size)) {
				std::cout << "[DiligentRenderer] Loaded shader: " << path << std::endl;
				return source;
			}
		}

		ERORR_MESSAGE(log::Category::Render, "Shaders", "Cannot load shader from the working directory or project root : %s", fileName);
		return {};
	}


	DiligentRenderer::DiligentRenderer()
		: m_impl(std::make_unique<Impl>()),
		m_viewports(std::make_unique<ViewportRegistry>()),
		m_engineViewportTexture(std::make_unique<RenderTexture>()),
		m_depthPreviewTexture(std::make_unique<RenderTexture>()) {
	}

	DiligentRenderer::~DiligentRenderer() {
		Shutdown();
	}

	void* DiligentRenderer::GetNativeDevice() const noexcept {
		return m_impl->m_renderDevice;
	}

	void* DiligentRenderer::GetNativeContext() const noexcept {
		return m_impl->m_deviceContext;
	}

	void* DiligentRenderer::GetNativeSwapChain() const noexcept {
		return m_impl->m_swapChain;
	}

	RenderViewport* DiligentRenderer::CreateViewport(GLFWwindow* window, uint32_t width, uint32_t height) {
		if (!window || !m_impl->m_renderDevice || !m_impl->m_deviceContext) return nullptr;
		Diligent::SwapChainDesc description = m_impl->m_swapChain->GetDesc();
		description.Width = width;
		description.Height = height;
		description.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;
		Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;
#if PLATFORM_WIN32
		auto* factory = Diligent::GetEngineFactoryD3D12();
		if (!factory) return nullptr;
		Diligent::Win32NativeWindow nativeWindow{ glfwGetWin32Window(window) };
		factory->CreateSwapChainD3D12(m_impl->m_renderDevice, m_impl->m_deviceContext, description,
			Diligent::FullScreenModeDesc{}, nativeWindow, &swapChain);
#else
		auto* factory = Diligent::GetEngineFactoryVk();
		if (!factory) return nullptr;
		Diligent::LinuxNativeWindow nativeWindow;
		if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
			nativeWindow.pDisplay = glfwGetWaylandDisplay();
			nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(glfwGetWaylandWindow(window)));
		}
		else {
			nativeWindow.pDisplay = glfwGetX11Display();
			nativeWindow.WindowId = static_cast<uint32_t>(glfwGetX11Window(window));
		}
		factory->CreateSwapChainVk(m_impl->m_renderDevice, m_impl->m_deviceContext, description,
			nativeWindow, &swapChain);
#endif
		if (!swapChain) return nullptr;
		const auto id = m_viewports->nextId++;
		ViewportRegistry::Entry entry{ std::move(swapChain), RenderViewport{} };
		entry.viewport.m_id = id;
		auto [it, inserted] = m_viewports->viewports.emplace(id, std::move(entry));
		return inserted ? &it->second.viewport : nullptr;
	}

	void DiligentRenderer::DestroyViewport(RenderViewport* viewport) {
		if (viewport) m_viewports->viewports.erase(viewport->m_id);
	}

	void DiligentRenderer::ResizeViewport(RenderViewport* viewport, uint32_t width, uint32_t height) {
		if (!viewport) return;
		auto it = m_viewports->viewports.find(viewport->m_id);
		if (it != m_viewports->viewports.end()) it->second.swapChain->Resize(width, height);
	}

	void DiligentRenderer::RenderViewportFrame(RenderViewport* viewport, void* drawData) {
		if (!viewport) return;
		auto it = m_viewports->viewports.find(viewport->m_id);
		if (it == m_viewports->viewports.end()) return;
		auto* renderTarget = it->second.swapChain->GetCurrentBackBufferRTV();
		const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
		m_impl->m_deviceContext->SetRenderTargets(1, &renderTarget, nullptr,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		m_impl->m_deviceContext->ClearRenderTarget(renderTarget, clearColor,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		if (m_viewports->drawCallback) {
			m_viewports->drawCallback(m_viewports->drawUserData, m_impl->m_deviceContext, drawData);
		}
	}

	void DiligentRenderer::PresentViewportFrame(RenderViewport* viewport) {
		if (!viewport) return;
		auto it = m_viewports->viewports.find(viewport->m_id);
		if (it != m_viewports->viewports.end()) it->second.swapChain->Present();
	}

	void DiligentRenderer::SetViewportDrawCallback(ViewportDrawCallback callback, void* userData) {
		m_viewports->drawCallback = callback;
		m_viewports->drawUserData = userData;
	}

	auto DiligentRenderer::Init(uint32_t width, uint32_t height, StringView title) -> EngineResult<void> {
		if (m_initialized) {
			return {};
		}

		m_width = width;
		m_height = height;

#if PLATFORM_WIN32
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
		if (!glfwInit()) {
			return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to initialize GLFW"));
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		m_window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.data(), nullptr, nullptr);
		if (!m_window) {
			glfwTerminate();
			return std::unexpected(EngineError(ErrorCode::WindowInitializationFailed, "Failed to create GLFW window"));
		}

#if PLATFORM_WIN32
		auto* pFactory = Diligent::GetEngineFactoryD3D12();
		if (!pFactory) {
			return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryD3D12"));
		}

		Diligent::EngineD3D12CreateInfo engineCreateInfo;
		engineCreateInfo.NumDeferredContexts = 0;
		engineCreateInfo.pRawMemAllocator = &g_Allocator;
		pFactory->CreateDeviceAndContextsD3D12(engineCreateInfo, &m_impl->m_renderDevice, &m_impl->m_deviceContext);
#else
		auto* pFactory = Diligent::GetEngineFactoryVk();
		if (!pFactory) {
			return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryVk"));
		}

		Diligent::EngineVkCreateInfo engineCreateInfo;
		engineCreateInfo.NumDeferredContexts = 0;
		engineCreateInfo.DynamicHeapSize = 32 << 20;
		engineCreateInfo.pRawMemAllocator = &g_Allocator;
		pFactory->CreateDeviceAndContextsVk(engineCreateInfo, &m_impl->m_renderDevice, &m_impl->m_deviceContext);
#endif

		if (!m_impl->m_renderDevice || !m_impl->m_deviceContext) {
			return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent Render Device & Contexts"));
		}

		Diligent::SwapChainDesc swapChainDesc;
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;

#if PLATFORM_WIN32
		Diligent::Win32NativeWindow nativeWindow{ glfwGetWin32Window(m_window) };
		pFactory->CreateSwapChainD3D12(m_impl->m_renderDevice, m_impl->m_deviceContext, swapChainDesc,
			Diligent::FullScreenModeDesc{}, nativeWindow, &m_impl->m_swapChain);
#else
		Diligent::LinuxNativeWindow nativeWindow;
		if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
			nativeWindow.pDisplay = glfwGetWaylandDisplay();
			nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(glfwGetWaylandWindow(m_window)));
		}
		else {
			nativeWindow.pDisplay = glfwGetX11Display();
			nativeWindow.WindowId = static_cast<uint32_t>(glfwGetX11Window(m_window));
		}

		pFactory->CreateSwapChainVk(m_impl->m_renderDevice, m_impl->m_deviceContext, swapChainDesc, nativeWindow, &m_impl->m_swapChain);
#endif

		if (!m_impl->m_swapChain) {
			return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent SwapChain"));
		}

		// Initialize 3D Rendering Pipeline
		InitPipeline();

		m_initialized = true;
		std::cout << "[DiligentRenderer] Diligent Engine, 3D Mesh Pipeline, and SOC Testbed initialized." << std::endl;
		return {};
	}

	void DiligentRenderer::InitPipeline() {
		CreateMeshBuffers();
		CreateInstanceBuffer();

		// Uniform Buffer for Camera
		Diligent::BufferDesc CBDesc;
		CBDesc.Name = "Camera Constants CB";
		CBDesc.Size = sizeof(Matrix4x4) + sizeof(Vector4);
		CBDesc.Usage = Diligent::USAGE_DYNAMIC;
		CBDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		CBDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		m_impl->m_renderDevice->CreateBuffer(CBDesc, nullptr, &m_impl->m_pCameraConstantsBuffer);

		// Create Shaders
		Diligent::ShaderCreateInfo ShaderCI;
		ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

		const String VSSource = LoadShaderSource("mesh.vert.hlsl");
		const String PSSource = LoadShaderSource("mesh.frag.hlsl");
		const String PSHighlightSource = LoadShaderSource("highlight.frag.hlsl");
		if (VSSource.empty() || PSSource.empty() || PSHighlightSource.empty()) {
			std::cerr << "[DiligentRenderer] Pipeline creation aborted: shader source is missing." << std::endl;
			return;
		}

		Diligent::RefCntAutoPtr<Diligent::IShader> pVS;
		{
			ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
			ShaderCI.Desc.Name = "Mesh VS";
			ShaderCI.Source = VSSource.c_str();
			m_impl->m_renderDevice->CreateShader(ShaderCI, &pVS);
		}

		Diligent::RefCntAutoPtr<Diligent::IShader> pPS;
		{
			ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
			ShaderCI.Desc.Name = "Mesh PS";
			ShaderCI.Source = PSSource.c_str();
			m_impl->m_renderDevice->CreateShader(ShaderCI, &pPS);
		}

		Diligent::RefCntAutoPtr<Diligent::IShader> pHighlightPS;
		{
			ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
			ShaderCI.Desc.Name = "Highlight PS";
			ShaderCI.Source = PSHighlightSource.c_str();
			m_impl->m_renderDevice->CreateShader(ShaderCI, &pHighlightPS);
		}

		if (!pVS || !pPS || !pHighlightPS) {
			std::cerr << "[DiligentRenderer] Pipeline creation aborted: shader compilation failed." << std::endl;
			return;
		}

		// Input Layout
		Diligent::LayoutElement LayoutElems[] = {
			// Slot 0: Per-vertex
			Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{2, 0, 2, Diligent::VT_FLOAT32, false},

			// Slot 1: Per-instance (Transform Matrix + Color)
			Diligent::LayoutElement{3, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
			Diligent::LayoutElement{4, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
			Diligent::LayoutElement{5, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
			Diligent::LayoutElement{6, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
			Diligent::LayoutElement{7, 1, 4, Diligent::VT_FLOAT32, false, Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}
		};

		// Main Opaque Pipeline State
		Diligent::GraphicsPipelineStateCreateInfo PSOCI;
		PSOCI.PSODesc.Name = "Opaque Mesh PSO";
		auto& Pipeline = PSOCI.GraphicsPipeline;
		Pipeline.NumRenderTargets = 1;
		Pipeline.RTVFormats[0] = m_impl->m_swapChain->GetDesc().ColorBufferFormat;
		Pipeline.DSVFormat = m_impl->m_swapChain->GetDesc().DepthBufferFormat;
		Pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		Pipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		Pipeline.DepthStencilDesc.DepthEnable = true;
		Pipeline.DepthStencilDesc.DepthWriteEnable = true;

		Pipeline.InputLayout.LayoutElements = LayoutElems;
		Pipeline.InputLayout.NumElements = _countof(LayoutElems);

		PSOCI.pVS = pVS;
		PSOCI.pPS = pPS;

		PSOCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		m_impl->m_renderDevice->CreateGraphicsPipelineState(PSOCI, &m_impl->m_pPSO);
		if (m_impl->m_pPSO) {
			auto* pVar = m_impl->m_pPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "CameraConstants");
			if (pVar) pVar->Set(m_impl->m_pCameraConstantsBuffer);
			m_impl->m_pPSO->CreateShaderResourceBinding(&m_impl->m_pSRB, true);
		}

		// Highlight (Culled Visualizer) Pipeline State
		Diligent::GraphicsPipelineStateCreateInfo HighlightPSOCI = PSOCI;
		HighlightPSOCI.PSODesc.Name = "Highlight Culled PSO";
		HighlightPSOCI.pPS = pHighlightPS;
		HighlightPSOCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		auto& Blend0 = HighlightPSOCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
		Blend0.BlendEnable = true;
		Blend0.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
		Blend0.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		Blend0.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
		Blend0.DestBlendAlpha = Diligent::BLEND_FACTOR_ZERO;
		m_impl->m_renderDevice->CreateGraphicsPipelineState(HighlightPSOCI, &m_impl->m_pHighlightPSO);

		// Create Depth Preview Texture
		CreateDepthPreviewTexture(m_depthPreviewWidth, m_depthPreviewHeight);
		CreateEngineViewport(m_engineViewportWidth, m_engineViewportHeight);
	}

	void DiligentRenderer::CreateMeshBuffers() {
		auto createBuffers = [this](const MeshData& mesh, Diligent::IBuffer** ppVB, Diligent::IBuffer** ppIB, uint32_t& indexCount) {
			Diligent::BufferDesc VBDesc;
			VBDesc.Name = "Mesh VB";
			VBDesc.Usage = Diligent::USAGE_IMMUTABLE;
			VBDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
			VBDesc.Size = mesh.vertices.size() * sizeof(Vertex);
			Diligent::BufferData VBData;
			VBData.pData = mesh.vertices.data();
			VBData.DataSize = VBDesc.Size;
			m_impl->m_renderDevice->CreateBuffer(VBDesc, &VBData, ppVB);

			Diligent::BufferDesc IBDesc;
			IBDesc.Name = "Mesh IB";
			IBDesc.Usage = Diligent::USAGE_IMMUTABLE;
			IBDesc.BindFlags = Diligent::BIND_INDEX_BUFFER;
			IBDesc.Size = mesh.indices.size() * sizeof(uint32_t);
			Diligent::BufferData IBData;
			IBData.pData = mesh.indices.data();
			IBData.DataSize = IBDesc.Size;
			m_impl->m_renderDevice->CreateBuffer(IBDesc, &IBData, ppIB);

			indexCount = static_cast<uint32_t>(mesh.indices.size());
			};

		const MeshData cubeMesh = GeometryPrimitives::CreateCube(1.0f);
		createBuffers(cubeMesh, &m_impl->m_pCubeVB, &m_impl->m_pCubeIB, m_cubeIndexCount);

		const MeshData wallMesh = GeometryPrimitives::CreateWall(1.0f, 1.0f, 1.0f);
		createBuffers(wallMesh, &m_impl->m_pWallVB, &m_impl->m_pWallIB, m_wallIndexCount);

		const MeshData groundMesh = GeometryPrimitives::CreateGroundPlane(160.0f, 160.0f);
		createBuffers(groundMesh, &m_impl->m_pGroundVB, &m_impl->m_pGroundIB, m_groundIndexCount);
	}

	void DiligentRenderer::CreateInstanceBuffer() {
		Diligent::BufferDesc InstBuffDesc;
		InstBuffDesc.Name = "Mesh Instance Buffer";
		InstBuffDesc.Usage = Diligent::USAGE_DYNAMIC;
		InstBuffDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		InstBuffDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		InstBuffDesc.Size = sizeof(GpuInstanceData) * MaxInstances;
		m_impl->m_renderDevice->CreateBuffer(InstBuffDesc, nullptr, &m_impl->m_pInstanceBuffer);
	}

	void DiligentRenderer::CreateDepthPreviewTexture(uint32_t width, uint32_t height) {
		if (m_impl->m_pDepthPreviewSRV) {
			m_impl->m_pDepthPreviewSRV->Release();
			m_impl->m_pDepthPreviewSRV = nullptr;
		}
		m_depthPreviewTexture->m_native = nullptr;
		if (m_impl->m_pDepthPreviewTex) {
			m_impl->m_pDepthPreviewTex->Release();
			m_impl->m_pDepthPreviewTex = nullptr;
		}

		m_depthPreviewWidth = width;
		m_depthPreviewHeight = height;
		m_depthPreviewPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000);

		Diligent::TextureDesc TexDesc;
		TexDesc.Name = "Software Depth Buffer Preview Texture";
		TexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
		TexDesc.Width = width;
		TexDesc.Height = height;
		TexDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
		TexDesc.Usage = Diligent::USAGE_DEFAULT;
		TexDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

		Diligent::TextureSubResData Level0Data;
		Level0Data.pData = m_depthPreviewPixels.data();
		Level0Data.Stride = width * sizeof(uint32_t);
		Diligent::TextureData InitData;
		InitData.pSubResources = &Level0Data;
		InitData.NumSubresources = 1;

		m_impl->m_renderDevice->CreateTexture(TexDesc, &InitData, &m_impl->m_pDepthPreviewTex);
		if (m_impl->m_pDepthPreviewTex) {
			m_impl->m_pDepthPreviewSRV = m_impl->m_pDepthPreviewTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
			if (m_impl->m_pDepthPreviewSRV) {
				m_impl->m_pDepthPreviewSRV->AddRef();
			}
		}
		m_depthPreviewTexture->m_native = m_impl->m_pDepthPreviewSRV;
		m_depthPreviewTexture->m_width = width;
		m_depthPreviewTexture->m_height = height;
	}

	void DiligentRenderer::CreateEngineViewport(uint32_t width, uint32_t height) {
		if (m_impl->m_pEngineViewportSRV) m_impl->m_pEngineViewportSRV->Release();
		if (m_impl->m_pEngineViewportDSV) m_impl->m_pEngineViewportDSV->Release();
		if (m_impl->m_pEngineViewportRTV) m_impl->m_pEngineViewportRTV->Release();
		if (m_impl->m_pEngineViewportTex) m_impl->m_pEngineViewportTex->Release();
		m_impl->m_pEngineViewportSRV = nullptr;
		m_engineViewportTexture->m_native = nullptr;
		m_impl->m_pEngineViewportDSV = nullptr;
		m_impl->m_pEngineViewportRTV = nullptr;
		m_impl->m_pEngineViewportTex = nullptr;
		m_engineViewportIsShaderResource = false;

		m_engineViewportWidth = width;
		m_engineViewportHeight = height;

		Diligent::TextureDesc colorDesc;
		colorDesc.Name = "Engine Viewport Color";
		colorDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
		colorDesc.Width = width;
		colorDesc.Height = height;
		colorDesc.Format = m_impl->m_swapChain->GetDesc().ColorBufferFormat;
		colorDesc.Usage = Diligent::USAGE_DEFAULT;
		colorDesc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
		m_impl->m_renderDevice->CreateTexture(colorDesc, nullptr, &m_impl->m_pEngineViewportTex);
		if (!m_impl->m_pEngineViewportTex) return;
		m_impl->m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

		m_impl->m_pEngineViewportRTV = m_impl->m_pEngineViewportTex->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		m_impl->m_pEngineViewportSRV = m_impl->m_pEngineViewportTex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
		m_engineViewportTexture->m_native = m_impl->m_pEngineViewportSRV;
		m_engineViewportTexture->m_width = width;
		m_engineViewportTexture->m_height = height;
		if (m_impl->m_pEngineViewportRTV) m_impl->m_pEngineViewportRTV->AddRef();
		if (m_impl->m_pEngineViewportSRV) m_impl->m_pEngineViewportSRV->AddRef();

		Diligent::TextureDesc depthDesc;
		depthDesc.Name = "Engine Viewport Depth";
		depthDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
		depthDesc.Width = width;
		depthDesc.Height = height;
		depthDesc.Format = m_impl->m_swapChain->GetDesc().DepthBufferFormat;
		depthDesc.Usage = Diligent::USAGE_DEFAULT;
		depthDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
		Diligent::RefCntAutoPtr<Diligent::ITexture> depthTexture;
		m_impl->m_renderDevice->CreateTexture(depthDesc, nullptr, &depthTexture);
		if (depthTexture) {
			depthTexture->SetState(Diligent::RESOURCE_STATE_DEPTH_WRITE);
			m_impl->m_pEngineViewportDSV = depthTexture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
			if (m_impl->m_pEngineViewportDSV) m_impl->m_pEngineViewportDSV->AddRef();
		}
	}

	void DiligentRenderer::UpdateDepthPreviewTexture() {
		if (!m_impl->m_pDepthPreviewTex || !m_impl->m_deviceContext) return;

		m_visibilitySystem.GetSoftwareOcclusionCuller().GetDepthBuffer().GenerateVisualTexture(m_depthPreviewPixels, m_depthPreviewFalseColor);

		Diligent::Box UpdateBox;
		UpdateBox.MinX = 0;
		UpdateBox.MaxX = m_depthPreviewWidth;
		UpdateBox.MinY = 0;
		UpdateBox.MaxY = m_depthPreviewHeight;

		Diligent::TextureSubResData SubresData;
		SubresData.Stride = m_depthPreviewWidth * sizeof(uint32_t);
		SubresData.pData = m_depthPreviewPixels.data();

		m_impl->m_deviceContext->UpdateTexture(m_impl->m_pDepthPreviewTex, 0, 0, UpdateBox, SubresData,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	bool DiligentRenderer::ShouldClose() const {
		return m_window ? glfwWindowShouldClose(m_window) : true;
	}

	void DiligentRenderer::BeginFrame() {
		if (!m_impl->m_swapChain || !m_impl->m_deviceContext) return;

		auto* pRTV = m_impl->m_swapChain->GetCurrentBackBufferRTV();
		auto* pDSV = m_impl->m_swapChain->GetDepthBufferDSV();

		const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
		m_impl->m_deviceContext->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		m_impl->m_deviceContext->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		m_impl->m_deviceContext->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	}

	void DiligentRenderer::RenderScene(const Camera& camera, const Scene& scene) {
		if (!m_impl->m_deviceContext || !m_impl->m_pPSO) return;

		if (m_impl->m_pEngineViewportRTV && m_impl->m_pEngineViewportDSV) {
			if (m_engineViewportIsShaderResource) {
				Diligent::StateTransitionDesc toRenderTarget{
					m_impl->m_pEngineViewportTex,
					Diligent::RESOURCE_STATE_SHADER_RESOURCE,
					Diligent::RESOURCE_STATE_RENDER_TARGET
				};
				m_impl->m_deviceContext->TransitionResourceStates(1, &toRenderTarget);
				m_engineViewportIsShaderResource = false;
			}
			m_impl->m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_RENDER_TARGET);

			const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
			m_impl->m_deviceContext->SetRenderTargets(1, &m_impl->m_pEngineViewportRTV, m_impl->m_pEngineViewportDSV,
				Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
			m_impl->m_deviceContext->ClearRenderTarget(m_impl->m_pEngineViewportRTV, clearColor,
				Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
			m_impl->m_deviceContext->ClearDepthStencil(m_impl->m_pEngineViewportDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
				Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
		}

		// 1. Run Software Occlusion Culling
		const Matrix4x4 cullingVP = camera.GetCullingViewProjection();
		Vector<OccludeeInstance> occludees;
		m_visibilitySystem.GetSoftwareOcclusionCuller().ExecuteCulling(scene, cullingVP, occludees);

		// 2. Update Depth Buffer Texture for ImGui
		UpdateDepthPreviewTexture();

		// 3. Update Camera Constant Buffer
		{
			struct CameraCBData {
				Matrix4x4 ViewProj;
				Vector4   CameraPos;
			};
			Diligent::MapHelper<CameraCBData> CBData(m_impl->m_deviceContext, m_impl->m_pCameraConstantsBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
			CBData->ViewProj = camera.GetViewProjectionMatrix();
			CBData->CameraPos = Vector4{ camera.GetPosition(), 1.0f };
		}

		// 4. Collect Visible and Culled Instances
		m_visibleGpuInstances.clear();
		m_culledGpuInstances.clear();

		for (const auto& inst : occludees) {
			if (inst.isVisible) {
				m_visibleGpuInstances.push_back({ inst.worldTransform, inst.color });
			}
			else {
				m_culledGpuInstances.push_back({ inst.worldTransform, Vector4{0.95f, 0.2f, 0.15f, 0.35f} });
			}
		}

		// 5. Render Ground & Occluders
		m_impl->m_deviceContext->SetPipelineState(m_impl->m_pPSO);
		m_impl->m_deviceContext->CommitShaderResources(m_impl->m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		// Draw Ground
		{
			GpuInstanceData groundInst{ Matrix4x4::Translation(Vector3{0.0f, 0.0f, 0.0f}), Vector4{0.22f, 0.24f, 0.27f, 1.0f} };
			{
				Diligent::MapHelper<GpuInstanceData> InstData(m_impl->m_deviceContext, m_impl->m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
				InstData[0] = groundInst;
			}

			const Diligent::Uint64 offsets[] = { 0, 0 };
			Diligent::IBuffer* pVBs[] = { m_impl->m_pGroundVB, m_impl->m_pInstanceBuffer };
			m_impl->m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			m_impl->m_deviceContext->SetIndexBuffer(m_impl->m_pGroundIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			Diligent::DrawIndexedAttribs DrawAttrs{ m_groundIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
			DrawAttrs.NumInstances = 1;
			m_impl->m_deviceContext->DrawIndexed(DrawAttrs);
		}

		// Draw Occluders (Walls) as one instanced batch to avoid repeated dynamic-buffer maps.
		const size_t numOccluders = (std::min)(occludees.size(), MaxInstances);
		if (numOccluders > 0) {
			Vector<GpuInstanceData> occluderGpuInstances;
			occluderGpuInstances.reserve(numOccluders);
			for (size_t i = 0; i < numOccluders; ++i) {
				occluderGpuInstances.push_back({ occludees[i].worldTransform, Vector4{0.35f, 0.38f, 0.44f, 1.0f} });
			}
			{
				Diligent::MapHelper<GpuInstanceData> InstData(m_impl->m_deviceContext, m_impl->m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
				std::memcpy(InstData, occluderGpuInstances.data(), numOccluders * sizeof(GpuInstanceData));
			}

			const Diligent::Uint64 offsets[] = { 0, 0 };
			Diligent::IBuffer* pVBs[] = { m_impl->m_pWallVB, m_impl->m_pInstanceBuffer };
			m_impl->m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			m_impl->m_deviceContext->SetIndexBuffer(m_impl->m_pWallIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			Diligent::DrawIndexedAttribs DrawAttrs{ m_wallIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
			DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numOccluders);
			m_impl->m_deviceContext->DrawIndexed(DrawAttrs);
		}

		// 6. Draw Occludees (Cubes)
		if (m_visibilitySystem.GetSoftwareOcclusionCuller().visualMode != VisualMode::OccludersOnly && !m_visibleGpuInstances.empty()) {
			const size_t numDraw = (std::min)(m_visibleGpuInstances.size(), MaxInstances);
			{
				Diligent::MapHelper<GpuInstanceData> InstData(m_impl->m_deviceContext, m_impl->m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
				memcpy(InstData, m_visibleGpuInstances.data(), numDraw * sizeof(GpuInstanceData));
			}

			const Diligent::Uint64 offsets[] = { 0, 0 };
			Diligent::IBuffer* pVBs[] = { m_impl->m_pCubeVB, m_impl->m_pInstanceBuffer };
			m_impl->m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			m_impl->m_deviceContext->SetIndexBuffer(m_impl->m_pCubeIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			Diligent::DrawIndexedAttribs DrawAttrs{ m_cubeIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
			DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numDraw);
			m_impl->m_deviceContext->DrawIndexed(DrawAttrs);
		}

		// 7. Highlight Culled Objects (Ghost visualization)
		if (m_visibilitySystem.GetSoftwareOcclusionCuller().visualMode == VisualMode::HighlightCulled && !m_culledGpuInstances.empty()) {
			const size_t numCulled = (std::min)(m_culledGpuInstances.size(), MaxInstances);
			{
				Diligent::MapHelper<GpuInstanceData> InstData(m_impl->m_deviceContext, m_impl->m_pInstanceBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
				memcpy(InstData, m_culledGpuInstances.data(), numCulled * sizeof(GpuInstanceData));
			}

			m_impl->m_deviceContext->SetPipelineState(m_impl->m_pHighlightPSO);
			m_impl->m_deviceContext->CommitShaderResources(m_impl->m_pSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			const Diligent::Uint64 offsets[] = { 0, 0 };
			Diligent::IBuffer* pVBs[] = { m_impl->m_pCubeVB, m_impl->m_pInstanceBuffer };
			m_impl->m_deviceContext->SetVertexBuffers(0, 2, pVBs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			m_impl->m_deviceContext->SetIndexBuffer(m_impl->m_pCubeIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			Diligent::DrawIndexedAttribs DrawAttrs{ m_cubeIndexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
			DrawAttrs.NumInstances = static_cast<Diligent::Uint32>(numCulled);
			m_impl->m_deviceContext->DrawIndexed(DrawAttrs);
		}

		if (m_impl->m_pEngineViewportTex) {
			Diligent::StateTransitionDesc toShaderResource{
				m_impl->m_pEngineViewportTex,
				Diligent::RESOURCE_STATE_RENDER_TARGET,
				Diligent::RESOURCE_STATE_SHADER_RESOURCE
			};
			m_impl->m_deviceContext->TransitionResourceStates(1, &toShaderResource);
			m_impl->m_pEngineViewportTex->SetState(Diligent::RESOURCE_STATE_SHADER_RESOURCE);
			m_engineViewportIsShaderResource = true;
		}
	}

	void DiligentRenderer::EndFrame() {
		if (!m_impl->m_swapChain || !m_impl->m_deviceContext) return;
		m_impl->m_swapChain->Present();
	}

	void DiligentRenderer::Shutdown() {
		if (!m_initialized) return;

		m_viewports->viewports.clear();
		m_viewports->drawCallback = nullptr;
		m_viewports->drawUserData = nullptr;

		if (m_impl->m_pDepthPreviewSRV) {
			m_impl->m_pDepthPreviewSRV->Release();
			m_impl->m_pDepthPreviewSRV = nullptr;
		}
		if (m_impl->m_pDepthPreviewTex) {
			m_impl->m_pDepthPreviewTex->Release();
			m_impl->m_pDepthPreviewTex = nullptr;
		}

		if (m_impl->m_pEngineViewportSRV) m_impl->m_pEngineViewportSRV->Release();
		if (m_impl->m_pEngineViewportDSV) m_impl->m_pEngineViewportDSV->Release();
		if (m_impl->m_pEngineViewportRTV) m_impl->m_pEngineViewportRTV->Release();
		if (m_impl->m_pEngineViewportTex) m_impl->m_pEngineViewportTex->Release();
		m_impl->m_pEngineViewportSRV = nullptr;
		m_impl->m_pEngineViewportDSV = nullptr;
		m_impl->m_pEngineViewportRTV = nullptr;
		m_impl->m_pEngineViewportTex = nullptr;

		if (m_impl->m_pInstanceBuffer) {
			m_impl->m_pInstanceBuffer->Release();
			m_impl->m_pInstanceBuffer = nullptr;
		}
		if (m_impl->m_pGroundIB) {
			m_impl->m_pGroundIB->Release();
			m_impl->m_pGroundIB = nullptr;
		}
		if (m_impl->m_pGroundVB) {
			m_impl->m_pGroundVB->Release();
			m_impl->m_pGroundVB = nullptr;
		}
		if (m_impl->m_pWallIB) {
			m_impl->m_pWallIB->Release();
			m_impl->m_pWallIB = nullptr;
		}
		if (m_impl->m_pWallVB) {
			m_impl->m_pWallVB->Release();
			m_impl->m_pWallVB = nullptr;
		}
		if (m_impl->m_pCubeIB) {
			m_impl->m_pCubeIB->Release();
			m_impl->m_pCubeIB = nullptr;
		}
		if (m_impl->m_pCubeVB) {
			m_impl->m_pCubeVB->Release();
			m_impl->m_pCubeVB = nullptr;
		}

		if (m_impl->m_pCameraConstantsBuffer) {
			m_impl->m_pCameraConstantsBuffer->Release();
			m_impl->m_pCameraConstantsBuffer = nullptr;
		}
		if (m_impl->m_pSRB) {
			m_impl->m_pSRB->Release();
			m_impl->m_pSRB = nullptr;
		}
		if (m_impl->m_pHighlightPSO) {
			m_impl->m_pHighlightPSO->Release();
			m_impl->m_pHighlightPSO = nullptr;
		}
		if (m_impl->m_pPSO) {
			m_impl->m_pPSO->Release();
			m_impl->m_pPSO = nullptr;
		}

		if (m_impl->m_swapChain) {
			m_impl->m_swapChain->Release();
			m_impl->m_swapChain = nullptr;
		}
		if (m_impl->m_deviceContext) {
			m_impl->m_deviceContext->Release();
			m_impl->m_deviceContext = nullptr;
		}
		if (m_impl->m_renderDevice) {
			m_impl->m_renderDevice->Release();
			m_impl->m_renderDevice = nullptr;
		}

		if (m_window) {
			glfwDestroyWindow(m_window);
			m_window = nullptr;
		}

		glfwTerminate();

		m_initialized = false;
		std::cout << "[DiligentRenderer] Shutdown completed." << std::endl;
	}

	size_t DiligentRenderer::GetMemAllocated() const {
		return g_Allocator.GetTotalAllocatedBytes();
	}

} // namespace Engine
