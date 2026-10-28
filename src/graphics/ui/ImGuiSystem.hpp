#pragma once

#include "core/Error.hpp"
#include "core/MessageBus.hpp"
#include "core/Settings.hpp"

#include "render/RenderSystem.hpp"
#include "render/api/FrameWriter.hpp"

#include "platform/Window.hpp"

#include "graphics/ui/ImGuiRenderer.hpp"
#include "graphics/ui/DockSpaceView.hpp"
#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/ProjectMenu.hpp"


#include <atomic>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

struct ImGuiViewport;
struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

namespace elm {

	class SocLabWindow;
	class OcclusionCullingSystem;

	// Threading:
	//  - main thread: Init, BuildFrame, Shutdown (render thread must be stopped), all ImGui/GLFW calls
	//  - render thread: RenderSystem executes the render graph using captured frame data
	// Platform (GLFW) windows are destroyed on the main thread only after the render thread released their swap chains.
	class ImGuiSystem {
	public:
		ImGuiSystem();
		~ImGuiSystem();

		ImGuiSystem(const ImGuiSystem&) = delete;
		ImGuiSystem& operator=(const ImGuiSystem&) = delete;

		[[nodiscard]] auto Init(platform::Window& window, render::RenderSystem& renderSystem, Settings& settings, StringView title,
			const OcclusionCullingSystem* cullingSystem = nullptr) -> elm::EngineResult<void>;
		// Main thread: runs UI logic and captures viewport events and draw data into frame.
		// Windows may add their own scene views (previews) to the frame.
		void BuildFrame(const ImGuiWindowContext& context);
		// Render thread: reports viewport surfaces released by the render graph
		void NotifyViewportSurfacesReleased(uint64_t count);
		void Shutdown();

		// Scene viewport state of the last built frame: camera input goes to the scene only while it is active
		[[nodiscard]] bool IsSceneViewportHovered() const noexcept;
		[[nodiscard]] bool IsSceneViewportFocused() const noexcept;

		// Window management
		void AddWindow(UniquePtr<IImGuiWindow> window);
		/// A window bound to the open project: CloseProjectWindows destroys it.
		void AddProjectWindow(UniquePtr<IImGuiWindow> window);
		/// Destroys the project windows; the windows of the next project open as these were.
		void CloseProjectWindows();
		/// The open project the Project menu shows and how it asks the engine for another one.
		void SetProjectMenu(ProjectMenuDesc project);
		template <typename T, typename... Args>
		[[nodiscard]] T& EmplaceWindow(Settings& settings, Args&&... args) {
			static_assert(std::is_base_of_v<IImGuiWindow, T>);
			auto window = MakeUnique<T>(settings, std::forward<Args>(args)...);
			T& result = *window;
			AddWindow(std::move(window));
			return result;
		}
		[[nodiscard]] std::span<const UniquePtr<IImGuiWindow>> GetWindows() const noexcept { return m_windows; }

		template <typename T>
		[[nodiscard]] T* GetWindow() const {
			for (const auto& window : m_windows) {
				if (auto* ptr = dynamic_cast<T*>(window.get())) {
					return ptr;
				}
			}
			return nullptr;
		}

	private:
		// ImGui platform callbacks, called on the main thread
		static void OnRendererCreateWindow(ImGuiViewport* viewport);
		static void OnRendererDestroyWindow(ImGuiViewport* viewport);
		static void OnPlatformDestroyWindow(ImGuiViewport* viewport);
		static float GetViewportDpiScale(ImGuiViewport* viewport);

		// imgui.ini section [Elm][...]: window visibility and the main window placement
		static void* OnSettingsReadOpen(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
		static void OnSettingsReadLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
		static void OnSettingsApplyAll(ImGuiContext* context, ImGuiSettingsHandler* handler);
		static void OnSettingsWriteAll(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer);

		// Main thread
		void CaptureViewports(render::OverlayFrame& frame);
		void DestroyReleasedPlatformWindows(bool all);
		void ApplySavedVisibility(IImGuiWindow& window) const;
		void ApplyMainWindowPlacement();
		void TrackMainWindowPlacement();

		struct MainWindowPlacement {
			int x{ 0 };
			int y{ 0 };
			int width{ 0 };
			int height{ 0 };
			bool maximized{ false };
			bool valid{ false };
		};

		// Platform window whose destruction waits until the render thread releases its swap chain
		struct PendingPlatformWindow {
			void* platformUserData{ nullptr };
			void* platformHandle{ nullptr };
			void* platformHandleRaw{ nullptr };
			uint64_t releaseSequence{ 0 };
		};

		GLFWwindow* m_window{ nullptr };
		UniquePtr<render::ImGuiRenderer> m_renderer;
		bool m_rendererInitialized{ false };
		bool m_imguiContextCreated{ false };
		bool m_initialized{ false };
		bool m_glfwInitialized{ false };
		String m_title;
		String m_iniFilePath;

		DockSpaceView m_dockSpace;
		ProjectMenu m_projectMenu;
		MessageBus m_windowMessageBus;
		Vector<UniquePtr<IImGuiWindow>> m_windows;
		// The windows of m_windows that CloseProjectWindows destroys
		Vector<const IImGuiWindow*> m_projectWindows;
		// Visibility read from imgui.ini, also for windows added after the load
		UnorderedMap<String, bool> m_savedVisibility;
		// Last restored (not maximized, not minimized) placement of the main window
		MainWindowPlacement m_mainWindowPlacement;

		// --- Main thread ---
		Vector<render::OverlaySurfaceEvent> m_pendingViewportEvents;
		Vector<PendingPlatformWindow> m_pendingPlatformWindows;
		uint64_t m_destroyedViewportCount{ 0 };
		void (*m_platformDestroyWindow)(ImGuiViewport*) { nullptr };
		bool m_destroyPlatformWindowsImmediately{ false };

		// --- Shared: number of Destroy events applied by the render thread ---
		std::atomic<uint64_t> m_releasedViewportCount{ 0 };
	};

} // namespace Engine
