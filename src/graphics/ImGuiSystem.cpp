#include "graphics/ImGuiSystem.hpp"
#include "graphics/render/ImGuiRenderer.hpp"
#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ui/LogWindow.hpp"
#include "graphics/ui/SceneHierarchyWindow.hpp"
#include "graphics/ui/SocLabWindow.hpp"
#include "graphics/ui/ProfilerWindow.hpp"

#include <GLFW/glfw3.h>
// Include Wayland and X11 system headers first to ensure types like wl_display or Window are defined
#if defined(__linux__)
    #include <wayland-client.h>
    #include <X11/Xlib.h>
// Expose native platform functions
#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif



#include <algorithm>
#include <filesystem>


#if PLATFORM_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"

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
		viewPort = {};
		fallbackTexture = {};
		releasedViewportCount = 0;
	}

	ImGuiSystem::ImGuiSystem() = default;

	ImGuiSystem::~ImGuiSystem() {
		Shutdown();
	}

	void ImGuiSystem::AddWindow( UniquePtr<IImGuiWindow> window ) {
		if ( window ) {
			window->Attach(m_windowMessageBus);
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
		m_title = title;

		if ( !ImGui::GetCurrentContext() ) {
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			m_imguiContextCreated = true;
		}

		// Register standard engine editor windows
		m_socLabWindow = &EmplaceWindow<SocLabWindow>( settings );
		EmplaceWindow<SceneHierarchyWindow>( settings );
		EmplaceWindow<EngineViewportWindow>( settings );
		EmplaceWindow<DepthPreviewWindow>( settings );
		EmplaceWindow<LogWindow>( settings );
		EmplaceWindow<ProfilerWindow>( settings );

		m_renderer = MakeUnique<render::ImGuiRenderer>();
		if ( !m_renderer->IsInitialized() ) {
			m_renderer.reset();
			Shutdown();
			return std::unexpected( elm::EngineError( elm::ErrorCode::UnknownError, "Failed to initialize ImGui renderer" ) );
		}
		m_rendererInitialized = true;
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

		// Renderer callbacks only record events; RenderSystem's backend applies and draws them on the render thread.
		// Secondary swap-chain resize events are captured with the frame and applied by the render thread.
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

	void ImGuiSystem::BuildFrame( RenderSystem& renderSystem, Scene& scene, Camera& camera, const FrameStats& stats,
		render::ViewPort& viewPort, ImGuiFrame& frame ) {
		// The frame slot was rendered already: its events are applied and draw lists are no longer used
		frame.Clear();
		if ( !m_initialized ) {
			frame.viewPort = viewPort.GetSnapshot();
			return;
		}
		if ( m_renderer ) {
			frame.fallbackTexture = m_renderer->GetFallbackTexture();
		}

		DestroyReleasedPlatformWindows( false );

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		m_dockSpace.Render( m_windows );
		for ( const auto& window : m_windows ) {
			if ( window && window->IsVisible() ) {
				ImGuiWindowContext context { renderSystem, scene, camera, stats, viewPort };
				window->Render( context );
			}
		}
		frame.viewPort = viewPort.GetSnapshot();

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
			if ( !isMain && ( !viewport->PlatformWindowCreated || ( viewport->Flags & ImGuiViewportFlags_IsMinimized ) ) ) continue;

			auto* window = isMain ? m_window : static_cast<GLFWwindow*>( viewport->PlatformHandle );
			if ( !window ) continue;
			uint32_t framebufferWidth = 1;
			uint32_t framebufferHeight = 1;
			getFramebufferSize( window, framebufferWidth, framebufferHeight );
			if ( !isMain ) {
				ImGuiViewportEvent resizeEvent;
				resizeEvent.type = ImGuiViewportEvent::Type::Resize;
				resizeEvent.id = viewport->ID;
				resizeEvent.width = framebufferWidth;
				resizeEvent.height = framebufferHeight;
				frame.viewportEvents.push_back( resizeEvent );
			}
			if ( !viewport->DrawData || viewport->DrawData->CmdListsCount == 0 ) continue;

			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize( window, &windowWidth, &windowHeight );

			auto& snapshot = frame.viewports.emplace_back();
			snapshot.id = viewport->ID;
			snapshot.isMain = isMain;
			snapshot.framebufferWidth = framebufferWidth;
			snapshot.framebufferHeight = framebufferHeight;
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

	void ImGuiSystem::NotifyViewportSurfacesReleased( uint64_t count ) {
		if ( !m_initialized ) return;
		const uint64_t releasedViewportCount = count;
		m_releasedViewportCount.fetch_add( releasedViewportCount, std::memory_order_release );
	}

	// --- Main thread, render thread must be stopped ---

	void ImGuiSystem::Shutdown() {
		if ( !m_rendererInitialized && !m_glfwInitialized && !m_initialized &&
			!m_imguiContextCreated && !m_renderer && m_windows.empty() ) return;
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
		if ( m_rendererInitialized ) {
			ImGui::DestroyPlatformWindows();
		}
		if ( m_renderer ) {
			m_renderer->Shutdown();
			m_renderer.reset();
		}
		if ( m_imguiContextCreated ) {
			ImGui::DestroyContext();
			m_imguiContextCreated = false;
		}
		m_rendererInitialized = false;
		m_pendingViewportEvents.clear();
		m_platformDestroyWindow = nullptr;
		m_windows.clear();
		m_socLabWindow = nullptr;
		m_initialized = false;
	}

} // namespace Engine
