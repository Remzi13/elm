#include "graphics/ui/ImGuiSystem.hpp"
#include "core/Unexpected.hpp"

#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/ui/EditorStyle.hpp"
#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ui/LogWindow.hpp"
#include "graphics/ui/ModelEditorWindow.hpp"
#include "graphics/ui/SceneHierarchyWindow.hpp"
#include "graphics/ui/SocLabWindow.hpp"
#include "graphics/ui/ProfilerWindow.hpp"
#include "graphics/ui/SettingsWindow.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"
#include "imgui_internal.h"

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

		render::TextureHandle getTextureHandle( ImTextureID textureId, render::TextureHandle fallback ) {
			const auto value = reinterpret_cast<uintptr_t>( textureId );
			if ( ( value & 1 ) == 0 ) return fallback;
			return render::TextureHandle::FromRaw( static_cast<uint32_t>( value >> 1 ) );
		}

	} // namespace

	ImGuiSystem::ImGuiSystem() = default;

	ImGuiSystem::~ImGuiSystem() {
		Shutdown();
	}

	void ImGuiSystem::AddWindow( UniquePtr<IImGuiWindow> window ) {
		if ( window ) {
			ApplySavedVisibility( *window );
			window->Attach(m_windowMessageBus);
			m_windows.push_back( std::move( window ) );
		}
	}

	//ImTextureID ImGuiSystem::ToTextureId( render::TextureHandle texture ) {
	//	if ( !texture.IsValid() ) return nullptr;
	//	// Real texture views are aligned pointers, so the lowest bit marks an engine handle
	//	const auto value = ( static_cast<uintptr_t>( texture.Raw() ) << 1 ) | 1;
	//	return reinterpret_cast<ImTextureID>( value );
	//}

	bool ImGuiSystem::IsSceneViewportHovered() const noexcept {
		const auto* window = GetWindow<EngineViewportWindow>();
		return window && window->IsHovered();
	}

	bool ImGuiSystem::IsSceneViewportFocused() const noexcept {
		const auto* window = GetWindow<EngineViewportWindow>();
		return window && window->IsFocused();
	}

	auto ImGuiSystem::Init( platform::Window& window, render::RenderSystem& renderSystem, Settings& settings, StringView title,
		const OcclusionCullingSystem* cullingSystem ) -> elm::EngineResult<void> {
		m_window = window.GetHandle();
		m_title = title;

		if ( !ImGui::GetCurrentContext() ) {
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			m_imguiContextCreated = true;
		}

		// Before the renderer builds the font atlas
		LoadEditorFonts();
		ApplyEditorStyle();

		// Register standard engine editor windows
		EmplaceWindow<SocLabWindow>( settings );
		EmplaceWindow<SceneHierarchyWindow>( settings );
		EmplaceWindow<EngineViewportWindow>( settings );
		EmplaceWindow<DepthPreviewWindow>( settings, cullingSystem );
		EmplaceWindow<LogWindow>( settings );
		EmplaceWindow<ProfilerWindow>( settings );
		EmplaceWindow<SettingsWindow>( settings );
		EmplaceWindow<ModelEditorWindow>( settings );

		m_renderer = MakeUnique<render::ImGuiRenderer>( renderSystem.Resources() );
		if ( !m_renderer->IsInitialized() ) {
			m_renderer.reset();
			Shutdown();
			return MakeUnexpected(ErrorCode::UnknownError, "Failed to initialize ImGui renderer",
				log::Category::Core, "ImGuiSystem");
		}
		m_rendererInitialized = true;
		auto& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable |
			ImGuiConfigFlags_DpiEnableScaleViewports | ImGuiConfigFlags_DpiEnableScaleFonts;
		io.ConfigViewportsNoAutoMerge = false;
		io.UserData = this;

		m_iniFilePath = FileSystem::Resolve( "imgui.ini" ).string();
		io.IniFilename = m_iniFilePath.c_str();

		ImGuiSettingsHandler settingsHandler;
		settingsHandler.TypeName = "Elm";
		settingsHandler.TypeHash = ImHashStr( "Elm" );
		settingsHandler.ReadOpenFn = &ImGuiSystem::OnSettingsReadOpen;
		settingsHandler.ReadLineFn = &ImGuiSystem::OnSettingsReadLine;
		settingsHandler.ApplyAllFn = &ImGuiSystem::OnSettingsApplyAll;
		settingsHandler.WriteAllFn = &ImGuiSystem::OnSettingsWriteAll;
		settingsHandler.UserData = this;
		ImGui::AddSettingsHandler( &settingsHandler );

		ImGui::LoadIniSettingsFromDisk( io.IniFilename );

		m_glfwInitialized = ImGui_ImplGlfw_InitForOther( m_window, true );
		if ( !m_glfwInitialized ) {
			Shutdown();
			return MakeUnexpected(ErrorCode::UnknownError, "Failed to initialize ImGui GLFW backend",
				log::Category::Core, "ImGuiSystem");
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

		render::OverlaySurfaceEvent event;
		event.type = render::OverlaySurfaceEvent::Type::Create;
		event.id = viewport->ID;
		getFramebufferSize( window, event.width, event.height );
		const auto native = platform::Window::GetNativeWindow( window );
		event.nativeHandle = native.handle;
		event.nativeDisplay = native.display;
		system->m_pendingViewportEvents.push_back( event );
	}

	void ImGuiSystem::OnRendererDestroyWindow( ImGuiViewport* viewport ) {
		auto* system = getSystem();
		if ( !system ) return;

		render::OverlaySurfaceEvent event;
		event.type = render::OverlaySurfaceEvent::Type::Destroy;
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

	// --- imgui.ini settings (main thread, inside LoadIniSettings*/SaveIniSettings*) ---

	namespace {
		// Entries of the [Elm][...] sections
		int windowsEntry = 0;
		int mainWindowEntry = 0;
	}

	void* ImGuiSystem::OnSettingsReadOpen( ImGuiContext*, ImGuiSettingsHandler*, const char* name ) {
		if ( std::strcmp( name, "Windows" ) == 0 ) return &windowsEntry;
		if ( std::strcmp( name, "MainWindow" ) == 0 ) return &mainWindowEntry;
		return nullptr;
	}

	void ImGuiSystem::OnSettingsReadLine( ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line ) {
		auto* system = static_cast<ImGuiSystem*>( handler->UserData );
		if ( entry == &windowsEntry ) {
			// "<window name>=<0|1>", the name may itself contain '='
			const char* separator = std::strrchr( line, '=' );
			if ( !separator || separator == line ) return;
			system->m_savedVisibility[String( line, separator )] = std::atoi( separator + 1 ) != 0;
			return;
		}
		if ( entry == &mainWindowEntry ) {
			auto& placement = system->m_mainWindowPlacement;
			int a = 0;
			int b = 0;
			if ( std::sscanf( line, "Pos=%d,%d", &a, &b ) == 2 ) {
				placement.x = a;
				placement.y = b;
			}
			else if ( std::sscanf( line, "Size=%d,%d", &a, &b ) == 2 ) {
				placement.width = a;
				placement.height = b;
				placement.valid = a > 0 && b > 0;
			}
			else if ( std::sscanf( line, "Maximized=%d", &a ) == 1 ) {
				placement.maximized = a != 0;
			}
		}
	}

	void ImGuiSystem::OnSettingsApplyAll( ImGuiContext*, ImGuiSettingsHandler* handler ) {
		auto* system = static_cast<ImGuiSystem*>( handler->UserData );
		for ( const auto& window : system->m_windows ) {
			if ( window ) system->ApplySavedVisibility( *window );
		}
		system->ApplyMainWindowPlacement();
	}

	void ImGuiSystem::OnSettingsWriteAll( ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer ) {
		auto* system = static_cast<ImGuiSystem*>( handler->UserData );

		// Windows that are not created in this run keep their saved state
		for ( const auto& window : system->m_windows ) {
			if ( window ) system->m_savedVisibility[String( window->GetName() )] = window->IsVisible();
		}
		buffer->append( "[Elm][Windows]\n" );
		for ( const auto& [name, visible] : system->m_savedVisibility ) {
			buffer->appendf( "%s=%d\n", name.c_str(), visible ? 1 : 0 );
		}
		buffer->append( "\n" );

		system->TrackMainWindowPlacement();
		const auto& placement = system->m_mainWindowPlacement;
		if ( placement.valid ) {
			buffer->append( "[Elm][MainWindow]\n" );
			buffer->appendf( "Pos=%d,%d\n", placement.x, placement.y );
			buffer->appendf( "Size=%d,%d\n", placement.width, placement.height );
			buffer->appendf( "Maximized=%d\n", placement.maximized ? 1 : 0 );
			buffer->append( "\n" );
		}
	}

	void ImGuiSystem::ApplySavedVisibility( IImGuiWindow& window ) const {
		const auto it = m_savedVisibility.find( String( window.GetName() ) );
		if ( it != m_savedVisibility.end() ) {
			window.SetVisible( it->second );
		}
	}

	void ImGuiSystem::ApplyMainWindowPlacement() {
		const auto& placement = m_mainWindowPlacement;
		if ( !m_window || !placement.valid ) return;

		// A monitor that was disconnected since the last run would leave the window out of sight
		bool onScreen = false;
		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors( &monitorCount );
		for ( int i = 0; i < monitorCount && !onScreen; ++i ) {
			int x = 0, y = 0, width = 0, height = 0;
			glfwGetMonitorWorkarea( monitors[i], &x, &y, &width, &height );
			onScreen = placement.x < x + width && placement.x + placement.width > x &&
				placement.y < y + height && placement.y + 32 > y;
		}

		glfwRestoreWindow( m_window );
		if ( onScreen ) {
			glfwSetWindowPos( m_window, placement.x, placement.y );
		}
		glfwSetWindowSize( m_window, placement.width, placement.height );
		if ( placement.maximized ) {
			glfwMaximizeWindow( m_window );
		}
	}

	void ImGuiSystem::TrackMainWindowPlacement() {
		if ( !m_window ) return;
		auto& placement = m_mainWindowPlacement;
		if ( glfwGetWindowAttrib( m_window, GLFW_ICONIFIED ) ) return;

		placement.maximized = glfwGetWindowAttrib( m_window, GLFW_MAXIMIZED ) != 0;
		// A maximized window keeps the restored placement for the next start
		if ( placement.maximized && placement.valid ) return;

		int x = 0, y = 0, width = 0, height = 0;
		glfwGetWindowPos( m_window, &x, &y );
		glfwGetWindowSize( m_window, &width, &height );
		if ( width <= 0 || height <= 0 ) return;
		placement = { x, y, width, height, placement.maximized, true };
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

	void ImGuiSystem::BuildFrame( const ImGuiWindowContext& context ) {
		auto& frame = context.frame.Overlay();
		frame.Clear();
		if ( !m_initialized ) return;
		if ( m_renderer ) {
			frame.fallbackTexture = m_renderer->GetFallbackTexture();
		}

		DestroyReleasedPlatformWindows( false );
		TrackMainWindowPlacement();

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		m_dockSpace.Render( m_windows );
		for ( const auto& window : m_windows ) {
			if ( window && window->IsVisible() ) {
				ImGuiWindowContext windowContext = context;
				window->Render( windowContext );
			}
		}
		ImGui::Render();
		// Creates/destroys platform windows, renderer callbacks record viewport events
		ImGui::UpdatePlatformWindows();

		// Events go with the draw data of the same frame, so the render thread sees them in order
		frame.surfaceEvents.swap( m_pendingViewportEvents );
		m_pendingViewportEvents.clear();

		CaptureViewports( frame );
	}

	void ImGuiSystem::CaptureViewports( render::OverlayFrame& frame ) {
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
				render::OverlaySurfaceEvent resizeEvent;
				resizeEvent.type = render::OverlaySurfaceEvent::Type::Resize;
				resizeEvent.id = viewport->ID;
				resizeEvent.width = framebufferWidth;
				resizeEvent.height = framebufferHeight;
				frame.surfaceEvents.push_back( resizeEvent );
			}
			if ( !viewport->DrawData || viewport->DrawData->CmdListsCount == 0 ||
				viewport->DrawData->DisplaySize.x <= 0.0f || viewport->DrawData->DisplaySize.y <= 0.0f ) continue;

			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize( window, &windowWidth, &windowHeight );

			auto& snapshot = frame.viewports.emplace_back();
			snapshot.id = viewport->ID;
			snapshot.isMain = isMain;
			snapshot.framebufferWidth = framebufferWidth;
			snapshot.framebufferHeight = framebufferHeight;
			auto framebufferScale = viewport->DrawData->FramebufferScale;
			if ( !isMain && windowWidth > 0 && windowHeight > 0 ) {
				framebufferScale = ImVec2(
					static_cast<float>( snapshot.framebufferWidth ) / static_cast<float>( windowWidth ),
					static_cast<float>( snapshot.framebufferHeight ) / static_cast<float>( windowHeight ) );
			}

			const auto& drawData = *viewport->DrawData;
			snapshot.drawLists.reserve( static_cast<size_t>( drawData.CmdListsCount ) );
			for ( int n = 0; n < viewport->DrawData->CmdListsCount; ++n ) {
				const auto& sourceList = *drawData.CmdLists[n];
				auto& drawList = snapshot.drawLists.emplace_back();
				drawList.vertices.reserve( static_cast<size_t>( sourceList.VtxBuffer.Size ) );
				for ( const ImDrawVert& sourceVertex : sourceList.VtxBuffer ) {
					const auto color = static_cast<uint32_t>( sourceVertex.col );
					constexpr float colorScale = 1.0f / 255.0f;
					drawList.vertices.push_back( {
						{
							( ( sourceVertex.pos.x - drawData.DisplayPos.x ) / drawData.DisplaySize.x ) * 2.0f - 1.0f,
							1.0f - ( ( sourceVertex.pos.y - drawData.DisplayPos.y ) / drawData.DisplaySize.y ) * 2.0f
						},
						{ sourceVertex.uv.x, sourceVertex.uv.y },
						{
							static_cast<float>( ( color >> IM_COL32_R_SHIFT ) & 0xff ) * colorScale,
							static_cast<float>( ( color >> IM_COL32_G_SHIFT ) & 0xff ) * colorScale,
							static_cast<float>( ( color >> IM_COL32_B_SHIFT ) & 0xff ) * colorScale,
							static_cast<float>( ( color >> IM_COL32_A_SHIFT ) & 0xff ) * colorScale
						}
					} );
				}

				drawList.indices.reserve( static_cast<size_t>( sourceList.IdxBuffer.Size ) );
				for ( const ImDrawIdx index : sourceList.IdxBuffer )
					drawList.indices.push_back( static_cast<uint32_t>( index ) );

				drawList.commands.reserve( static_cast<size_t>( sourceList.CmdBuffer.Size ) );
				for ( const ImDrawCmd& sourceCommand : sourceList.CmdBuffer ) {
					drawList.commands.push_back( {
						{
							( sourceCommand.ClipRect.x - drawData.DisplayPos.x ) * framebufferScale.x,
							( sourceCommand.ClipRect.y - drawData.DisplayPos.y ) * framebufferScale.y,
							( sourceCommand.ClipRect.z - drawData.DisplayPos.x ) * framebufferScale.x,
							( sourceCommand.ClipRect.w - drawData.DisplayPos.y ) * framebufferScale.y
						},
						static_cast<uint32_t>( sourceCommand.IdxOffset ),
						static_cast<uint32_t>( sourceCommand.VtxOffset ),
						static_cast<uint32_t>( sourceCommand.ElemCount ),
						getTextureHandle( sourceCommand.TextureId, frame.fallbackTexture ),
						sourceCommand.UserCallback != nullptr
					} );
				}
			}
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
			auto& io = ImGui::GetIO();
			if ( io.IniFilename != nullptr ) {
				ImGui::SaveIniSettingsToDisk( io.IniFilename );
				// The layout is saved while every window still exists; DestroyContext must not
				// write it again from the half torn down state (or call back into this system)
				io.IniFilename = nullptr;
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
		m_initialized = false;
	}

} // namespace Engine
