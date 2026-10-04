#pragma once

#include "core/Error.hpp"
#include "core/MessageBus.hpp"

#include "render/RenderSystem.hpp"
#include "render/OverlayFrame.hpp"


#include "graphics/ui/ImGuiRenderer.hpp"
#include "graphics/ui/DockSpaceView.hpp"
#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/Settings.hpp"


#include "imgui.h"

#include <atomic>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace elm {

	class SocLabWindow;

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

		[[nodiscard]] auto Init(RenderSystem& renderSystem, Settings& settings, StringView title) -> elm::EngineResult<void>;
		// Main thread: runs UI logic and captures viewport events and draw data into frame
		void BuildFrame(RenderSystem& renderSystem, Scene& scene, Camera& camera, const FrameStats& stats,
			render::ViewPort& viewPort, render::OverlayFrame& frame);
		// Render thread: reports viewport surfaces released by the render graph
		void NotifyViewportSurfacesReleased(uint64_t count);
		void Shutdown();

		// Texture reference usable in ImGui::Image; resolved to a GPU view on the render thread
		[[nodiscard]] static ImTextureID ToTextureId(core::Handler texture);

		// Window management
		void AddWindow(UniquePtr<IImGuiWindow> window);
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

		// Main thread
		void CaptureViewports(render::OverlayFrame& frame);
		void DestroyReleasedPlatformWindows(bool all);

		struct SavedLabSettings {
			int preset{ 0 };
			int instanceCount{ 1000 };
			bool enableFrustum{ true };
			bool enableOcclusion{ true };
			float depthBias{ 0.0f };
			int resolution{ 2 };
			int visualMode{ 0 };
			bool depthFalseColor{ true };
			float moveSpeed{ 10.0f };
			bool hasLoaded{ false };
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
		SavedLabSettings m_savedSettings;

		DockSpaceView m_dockSpace;
		MessageBus m_windowMessageBus;
		Vector<UniquePtr<IImGuiWindow>> m_windows;		

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
