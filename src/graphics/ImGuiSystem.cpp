#include "graphics/ImGuiSystem.hpp"
#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ui/SocLabWindow.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>


#if PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
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

		void SetRenderSurface( Uint32 width, Uint32 height, SURFACE_TRANSFORM transform ) {
			m_pRenderer->NewFrame( width, height, transform );
		}

		void RenderDrawData( IDeviceContext* context, ImDrawData* drawData ) {
			m_pRenderer->RenderDrawData( context, drawData );
		}
	};

} // namespace Diligent

namespace elm {

	namespace {

		ImGuiSystem* getSystem() {
			return ImGui::GetCurrentContext() ? static_cast<ImGuiSystem*>( ImGui::GetIO().UserData ) : nullptr;
		}

		void getFramebufferSize( GLFWwindow* window, uint32_t& width, uint32_t& height ) {
			int framebufferWidth = 0;
			int framebufferHeight = 0;
			glfwGetFramebufferSize( window, &framebufferWidth, &framebufferHeight );
			width = static_cast<uint32_t>( (std::max)( framebufferWidth, 1 ) );
			height = static_cast<uint32_t>( (std::max)( framebufferHeight, 1 ) );
		}

	} // namespace

	void ImGuiFrame::Clear() {
		for ( auto& snapshot : viewports ) {
			for ( ImDrawList* list : snapshot.drawLists ) {
				IM_DELETE( list );
			}
		}
		viewports.clear();
		viewportEvents.clear();
	}

	ImGuiSystem::ImGuiSystem() = default;

	ImGuiSystem::~ImGuiSystem() {
		Shutdown();
	}

	void ImGuiSystem::AddWindow( UniquePtr<IImGuiWindow> window ) {
		if ( window ) {
			m_windows.push_back( std::move( window ) );
		}
	}

	ImTextureID ImGuiSystem::ToTextureId( core::Handler texture ) {
		if ( !texture.IsValid() ) return nullptr;
		// Real texture views (e.g. the font atlas) are aligned pointers, so the lowest bit marks a handler
		const auto value = ( static_cast<uintptr_t>( texture.GetValue() ) << 1 ) | 1;
		return reinterpret_cast<ImTextureID>( value );
	}

	auto ImGuiSystem::Init( RenderSystem& renderSystem, Settings& settings, StringView title ) -> elm::EngineResult<void> {
		m_window = renderSystem.GetWindowHandle();
		m_renderSystem = &renderSystem;
		m_settings = &settings;
		m_title = title;

		// Register standard engine editor windows
		auto socLab = MakeUnique<SocLabWindow>(settings);
		m_socLabWindow = socLab.get();
		AddWindow( std::move( socLab ) );
		AddWindow( MakeUnique<EngineViewportWindow>(settings) );
		AddWindow( MakeUnique<DepthPreviewWindow>(settings) );

		// Renderer device objects (including the font atlas) are created here, before the render thread starts
		const auto& swapChainDesc = renderSystem.GetSwapChain()->GetDesc();
		Diligent::ImGuiDiligentCreateInfo createInfo;
		createInfo.pDevice = renderSystem.GetRenderDevice();
		createInfo.BackBufferFmt = swapChainDesc.ColorBufferFormat;
		createInfo.DepthBufferFmt = Diligent::TEX_FORMAT_UNKNOWN;
		m_imGui = MakeUnique<Diligent::ImGuiImplDiligentViewport>( createInfo );
		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable |
			ImGuiConfigFlags_DpiEnableScaleViewports | ImGuiConfigFlags_DpiEnableScaleFonts;
		io.ConfigViewportsNoAutoMerge = false;
		io.UserData = this;

		const std::filesystem::path sourceRoot =
			std::filesystem::path{ __FILE__ }.parent_path().parent_path().parent_path();
		if ( std::filesystem::exists( sourceRoot / "CMakeLists.txt" ) ) {
			m_iniFilePath = ( sourceRoot / "imgui.ini" ).string();
		}
		else {
			m_iniFilePath = "imgui.ini";
		}
		io.IniFilename = m_iniFilePath.c_str();


		ImGui::LoadIniSettingsFromDisk( io.IniFilename );

		ImGui::StyleColorsDark();
		m_glfwInitialized = ImGui_ImplGlfw_InitForOther( m_window, true );
		if ( !m_glfwInitialized ) {
			Shutdown();
			return std::unexpected( elm::EngineError( elm::ErrorCode::UnknownError, "Failed to initialize ImGui GLFW backend" ) );
		}
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		// Renderer callbacks only record events: swap chains live on the render thread and are drawn in RenderFrame().
		// Swap chain resize is not needed as a callback: RenderFrame() matches it to the captured framebuffer size.
		auto& platformIO = ImGui::GetPlatformIO();
		platformIO.Renderer_CreateWindow = &ImGuiSystem::OnRendererCreateWindow;
		platformIO.Renderer_DestroyWindow = &ImGuiSystem::OnRendererDestroyWindow;
		platformIO.Platform_GetWindowDpiScale = &ImGuiSystem::GetViewportDpiScale;
		// GLFW window destruction is deferred until the render thread no longer presents to it
		m_platformDestroyWindow = platformIO.Platform_DestroyWindow;
		platformIO.Platform_DestroyWindow = &ImGuiSystem::OnPlatformDestroyWindow;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		m_initialized = true;
		return {};
	}

	// --- ImGui platform callbacks (main thread, inside NewFrame/UpdatePlatformWindows/DestroyPlatformWindows) ---

	void ImGuiSystem::OnRendererCreateWindow( ImGuiViewport* viewport ) {
		auto* system = getSystem();
		auto* window = static_cast<GLFWwindow*>( viewport->PlatformHandle );
		if ( !system || !window ) return;

		ImGuiViewportEvent event;
		event.type = ImGuiViewportEvent::Type::Create;
		event.id = viewport->ID;
		getFramebufferSize( window, event.width, event.height );
#if PLATFORM_WIN32
		event.nativeHandle = glfwGetWin32Window( window );
#else
		if ( glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ) {
			event.nativeDisplay = glfwGetWaylandDisplay();
			event.nativeHandle = glfwGetWaylandWindow( window );
		}
		else {
			event.nativeDisplay = glfwGetX11Display();
			event.nativeHandle = reinterpret_cast<void*>( static_cast<uintptr_t>( glfwGetX11Window( window ) ) );
		}
#endif
		system->m_pendingViewportEvents.push_back( event );
	}

	void ImGuiSystem::OnRendererDestroyWindow( ImGuiViewport* viewport ) {
		auto* system = getSystem();
		if ( !system ) return;

		ImGuiViewportEvent event;
		event.type = ImGuiViewportEvent::Type::Destroy;
		event.id = viewport->ID;
		system->m_pendingViewportEvents.push_back( event );
		++system->m_destroyedViewportCount;
	}

	// Called right after OnRendererDestroyWindow(). The render thread may still present to this window
	// (previous frame) and releases its swap chain only when it applies the Destroy event, so the window is
	// hidden now and destroyed in DestroyReleasedPlatformWindows().
	void ImGuiSystem::OnPlatformDestroyWindow( ImGuiViewport* viewport ) {
		auto* system = getSystem();
		if ( !system || !system->m_platformDestroyWindow ) return;

		if ( system->m_destroyPlatformWindowsImmediately || !viewport->PlatformUserData ) {
			system->m_platformDestroyWindow( viewport );
			return;
		}

		if ( auto* window = static_cast<GLFWwindow*>( viewport->PlatformHandle ) ) {
			glfwHideWindow( window );
		}
		system->m_pendingPlatformWindows.push_back( {
			viewport->PlatformUserData,
			viewport->PlatformHandle,
			viewport->PlatformHandleRaw,
			system->m_destroyedViewportCount
		} );
		// ImGui expects the platform data to be gone after this call
		viewport->PlatformUserData = nullptr;
		viewport->PlatformHandle = nullptr;
		viewport->PlatformHandleRaw = nullptr;
	}

	float ImGuiSystem::GetViewportDpiScale( ImGuiViewport* viewport ) {
		auto* window = static_cast<GLFWwindow*>( viewport->PlatformHandle );
		if ( !window ) return 1.0f;

		float xScale = 1.0f;
		float yScale = 1.0f;
		glfwGetWindowContentScale( window, &xScale, &yScale );
		return xScale > 0.0f ? xScale : 1.0f;
	}

	// --- Main thread ---

	void ImGuiSystem::DestroyReleasedPlatformWindows( bool all ) {
		const uint64_t released = m_releasedViewportCount.load( std::memory_order_acquire );
		size_t count = 0;
		for ( const auto& pending : m_pendingPlatformWindows ) {
			if ( !all && pending.releaseSequence > released ) break;

			// The GLFW backend only needs the platform fields of the viewport, which itself may be already gone
			ImGuiViewport viewport;
			viewport.PlatformUserData = pending.platformUserData;
			viewport.PlatformHandle = pending.platformHandle;
			viewport.PlatformHandleRaw = pending.platformHandleRaw;
			m_platformDestroyWindow( &viewport );
			++count;
		}
		m_pendingPlatformWindows.erase( m_pendingPlatformWindows.begin(), m_pendingPlatformWindows.begin() + count );
	}

	void ImGuiSystem::BuildFrame( RenderSystem& renderSystem, Scene& scene, const FrameStats& stats, ImGuiFrame& frame ) {
		// The frame slot was rendered already: its events are applied and draw lists are no longer used
		frame.Clear();
		if ( !m_initialized ) return;

		DestroyReleasedPlatformWindows( false );

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		m_dockSpace.Render( m_windows );
		for ( const auto& window : m_windows ) {
			if ( window && window->IsVisible() ) {
				window->Render( renderSystem, scene, stats );
			}
		}

		ImGui::Render();
		// Creates/destroys platform windows, renderer callbacks record viewport events
		ImGui::UpdatePlatformWindows();

		// Events go with the draw data of the same frame, so the render thread sees them in order
		frame.viewportEvents.swap( m_pendingViewportEvents );
		m_pendingViewportEvents.clear();

		CaptureViewports( frame );
	}

	void ImGuiSystem::CaptureViewports( ImGuiFrame& frame ) {
		auto& platformIO = ImGui::GetPlatformIO();
		for ( int i = 0; i < platformIO.Viewports.Size; ++i ) {
			ImGuiViewport* viewport = platformIO.Viewports[i];
			const bool isMain = ( i == 0 );
			if ( !viewport->DrawData || viewport->DrawData->CmdListsCount == 0 ) continue;
			if ( !isMain && ( !viewport->PlatformWindowCreated || ( viewport->Flags & ImGuiViewportFlags_IsMinimized ) ) ) continue;

			auto* window = isMain ? m_window : static_cast<GLFWwindow*>( viewport->PlatformHandle );
			if ( !window ) continue;

			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize( window, &windowWidth, &windowHeight );

			auto& snapshot = frame.viewports.emplace_back();
			snapshot.id = viewport->ID;
			snapshot.isMain = isMain;
			getFramebufferSize( window, snapshot.framebufferWidth, snapshot.framebufferHeight );
			snapshot.drawData = *viewport->DrawData;
			if ( !isMain && windowWidth > 0 && windowHeight > 0 ) {
				snapshot.drawData.FramebufferScale = ImVec2(
					static_cast<float>( snapshot.framebufferWidth ) / static_cast<float>( windowWidth ),
					static_cast<float>( snapshot.framebufferHeight ) / static_cast<float>( windowHeight ) );
			}

			// ImGui reuses its draw lists in the next NewFrame(), so the render thread gets copies
			snapshot.drawLists.reserve( static_cast<size_t>( viewport->DrawData->CmdListsCount ) );
			for ( int n = 0; n < viewport->DrawData->CmdListsCount; ++n ) {
				snapshot.drawLists.push_back( viewport->DrawData->CmdLists[n]->CloneOutput() );
			}
			snapshot.drawData.CmdLists = nullptr; // set in RenderFrame(): the snapshot may still move
			snapshot.drawData.OwnerViewport = nullptr;
		}
	}

	// --- Render thread ---

	void ImGuiSystem::ApplyViewportEvents( RenderSystem& renderSystem, const Vector<ImGuiViewportEvent>& events ) {
		for ( const auto& event : events ) {
			auto it = m_viewportSwapChains.find( event.id );
			if ( it != m_viewportSwapChains.end() ) {
				if ( it->second ) it->second->Release();
				m_viewportSwapChains.erase( it );
			}

			if ( event.type == ImGuiViewportEvent::Type::Destroy ) {
				// Lets the main thread destroy the platform window
				m_releasedViewportCount.fetch_add( 1, std::memory_order_release );
				continue;
			}

			Diligent::SwapChainDesc description = renderSystem.GetSwapChain()->GetDesc();
			description.Width = event.width;
			description.Height = event.height;
			description.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;

			Diligent::ISwapChain* swapChain = nullptr;
#if PLATFORM_WIN32
			Diligent::Win32NativeWindow nativeWindow{ event.nativeHandle };
			Diligent::GetEngineFactoryD3D12()->CreateSwapChainD3D12( renderSystem.GetRenderDevice(),
				renderSystem.GetDeviceContext(), description,
				Diligent::FullScreenModeDesc{}, nativeWindow, &swapChain );
#else
			Diligent::LinuxNativeWindow nativeWindow;
			nativeWindow.pDisplay = event.nativeDisplay;
			nativeWindow.WindowId = static_cast<uint32_t>( reinterpret_cast<uintptr_t>( event.nativeHandle ) );
			Diligent::GetEngineFactoryVk()->CreateSwapChainVk( renderSystem.GetRenderDevice(),
				renderSystem.GetDeviceContext(), description,
				nativeWindow, &swapChain );
#endif
			m_viewportSwapChains[event.id] = swapChain;
		}
	}

	void ImGuiSystem::ReleaseViewportSwapChains() {
		for ( auto& [id, swapChain] : m_viewportSwapChains ) {
			if ( swapChain ) swapChain->Release();
		}
		m_viewportSwapChains.clear();
	}

	void ImGuiSystem::ResolveTextureIds( RenderSystem& renderSystem, ImGuiViewportSnapshot& snapshot ) {
		for ( ImDrawList* list : snapshot.drawLists ) {
			for ( ImDrawCmd& cmd : list->CmdBuffer ) {
				const auto value = reinterpret_cast<uintptr_t>( cmd.TextureId );
				if ( ( value & 1 ) == 0 ) continue;

				const core::Handler handler( static_cast<core::Handler::ValueType>( value >> 1 ), core::Handler::Render );
				cmd.TextureId = renderSystem.GetTextureView( handler );
				if ( !cmd.TextureId ) {
					// Texture is not created yet or already destroyed
					cmd.ElemCount = 0;
				}
			}
		}
	}

	void ImGuiSystem::RenderFrame( RenderSystem& renderSystem, ImGuiFrame& frame ) {
		if ( !m_initialized ) return;

		ApplyViewportEvents( renderSystem, frame.viewportEvents );

		auto* context = renderSystem.GetDeviceContext();
		const float clearColor[] = { 0.11f, 0.13f, 0.16f, 1.0f };
		Vector<Diligent::ISwapChain*> presentList;

		for ( auto& snapshot : frame.viewports ) {
			Diligent::ISwapChain* swapChain = nullptr;
			if ( snapshot.isMain ) {
				swapChain = renderSystem.GetSwapChain();
			}
			else if ( auto it = m_viewportSwapChains.find( snapshot.id ); it != m_viewportSwapChains.end() ) {
				swapChain = it->second;
			}
			if ( !swapChain ) continue;

			// Main swap chain belongs to RenderSystem; viewport ones follow the window size captured this frame
			if ( !snapshot.isMain ) {
				const auto& desc = swapChain->GetDesc();
				if ( desc.Width != snapshot.framebufferWidth || desc.Height != snapshot.framebufferHeight ) {
					swapChain->Resize( snapshot.framebufferWidth, snapshot.framebufferHeight );
				}
			}

			auto* renderTarget = swapChain->GetCurrentBackBufferRTV();
			if ( !renderTarget ) continue;

			ResolveTextureIds( renderSystem, snapshot );
			snapshot.drawData.CmdLists = snapshot.drawLists.data();

			m_imGui->SetRenderSurface( snapshot.framebufferWidth, snapshot.framebufferHeight, swapChain->GetDesc().PreTransform );
			context->SetRenderTargets( 1, &renderTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
			if ( !snapshot.isMain ) {
				context->ClearRenderTarget( renderTarget, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
				presentList.push_back( swapChain );
			}
			m_imGui->RenderDrawData( context, &snapshot.drawData );
		}

		// Main swap chain is presented by RenderSystem::EndFrame()
		for ( auto* swapChain : presentList ) {
			swapChain->Present();
		}
	}

	// --- Main thread, render thread must be stopped ---

	void ImGuiSystem::Shutdown() {
		// Swap chains first: their windows are destroyed below
		ReleaseViewportSwapChains();
		m_destroyPlatformWindowsImmediately = true;
		if ( m_platformDestroyWindow ) {
			DestroyReleasedPlatformWindows( true );
		}

		if ( m_initialized ) {
			const auto& io = ImGui::GetIO();
			if ( io.IniFilename != nullptr ) {
				ImGui::SaveIniSettingsToDisk( io.IniFilename );
			}
		}
		if ( m_glfwInitialized ) {
			// Destroys the remaining platform windows through OnPlatformDestroyWindow()
			ImGui_ImplGlfw_Shutdown();
			m_glfwInitialized = false;
		}
		if ( m_imGui ) ImGui::DestroyPlatformWindows();
		m_imGui.reset();
		m_pendingViewportEvents.clear();
		m_platformDestroyWindow = nullptr;
		m_windows.clear();
		m_socLabWindow = nullptr;
		m_renderSystem = nullptr;
		m_initialized = false;
	}

} // namespace Engine
