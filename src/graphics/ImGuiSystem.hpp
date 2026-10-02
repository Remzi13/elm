#pragma once

#include "core/Error.hpp"
#include "core/MessageBus.hpp"

#include "graphics/RenderSystem.hpp"
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

namespace render {
class ImGuiRenderer;
}

	class SocLabWindow;

	// Lifetime change of an ImGui platform viewport, recorded on the main thread and applied on the render thread
	struct ImGuiViewportEvent {
		enum class Type {
			Create,
			Destroy,
			Resize
		};

		Type type{ Type::Create };
		ImGuiID id{ 0 };
		// Native window resolved on the main thread: HWND / X11 Window / wl_surface* and X11 Display* / wl_display*
		void* nativeHandle{ nullptr };
		void* nativeDisplay{ nullptr };
		uint32_t width{ 1 };
		uint32_t height{ 1 };
	};

	// Copy of the ImGui draw data of one platform viewport
	struct ImGuiViewportSnapshot {
		ImGuiID id{ 0 };
		bool isMain{ false };
		ImDrawData drawData;
		Vector<ImDrawList*> drawLists;
		uint32_t framebufferWidth{ 1 };
		uint32_t framebufferHeight{ 1 };
	};

	// ImGui output of one frame. Every built frame must be rendered: it carries viewport events.
	// Draw lists are allocated by ImGui, so Clear() must be called on the main thread.
	struct ImGuiFrame {
		ImGuiFrame() = default;
		~ImGuiFrame() { Clear(); }

		ImGuiFrame(const ImGuiFrame&) = delete;
		ImGuiFrame& operator=(const ImGuiFrame&) = delete;

		void Clear();

		Vector<ImGuiViewportEvent> viewportEvents; // applied before drawing, in order
		Vector<ImGuiViewportSnapshot> viewports;   // [0] - main viewport
	};

	// Threading:
	//  - main thread: Init, BuildFrame, Shutdown (render thread must be stopped), all ImGui/GLFW calls
	//  - render thread: RenderFrame; RenderSystem's ImGui backend owns secondary viewport swap chains
	// Platform (GLFW) windows are destroyed on the main thread only after the render thread released their swap chains.
	class ImGuiSystem {
	public:
		ImGuiSystem();
		~ImGuiSystem();

		ImGuiSystem(const ImGuiSystem&) = delete;
		ImGuiSystem& operator=(const ImGuiSystem&) = delete;

		[[nodiscard]] auto Init(RenderSystem& renderSystem, Settings& settings, StringView title) -> elm::EngineResult<void>;
		// Main thread: runs UI logic and captures viewport events and draw data into frame
		void BuildFrame(RenderSystem& renderSystem, Scene& scene, const Camera& camera, const FrameStats& stats, ImGuiFrame& frame);
		// Render thread: applies viewport events, draws and presents secondary viewports
		void RenderFrame(RenderSystem& renderSystem, ImGuiFrame& frame);
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
		void CaptureViewports(ImGuiFrame& frame);
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
		RenderSystem* m_renderSystem{ nullptr };
		UniquePtr<render::ImGuiRenderer> m_renderer;
		bool m_rendererInitialized{ false };
		bool m_initialized{ false };
		bool m_glfwInitialized{ false };
		String m_title;
		String m_iniFilePath;
		SavedLabSettings m_savedSettings;

		DockSpaceView m_dockSpace;
		MessageBus m_windowMessageBus;
		Vector<UniquePtr<IImGuiWindow>> m_windows;
		SocLabWindow* m_socLabWindow{ nullptr };

		// --- Main thread ---
		Vector<ImGuiViewportEvent> m_pendingViewportEvents;
		Vector<PendingPlatformWindow> m_pendingPlatformWindows;
		uint64_t m_destroyedViewportCount{ 0 };
		void (*m_platformDestroyWindow)(ImGuiViewport*) { nullptr };
		bool m_destroyPlatformWindowsImmediately{ false };

		// --- Shared: number of Destroy events applied by the render thread ---
		std::atomic<uint64_t> m_releasedViewportCount{ 0 };
	};

} // namespace Engine
