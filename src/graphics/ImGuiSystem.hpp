#pragma once

#include "core/Error.hpp"
#include "graphics/RenderSystem.hpp"
#include "graphics/ui/DockSpaceView.hpp"
#include "graphics/ui/IImGuiWindow.hpp"
#include "imgui.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Diligent {
	class ImGuiImplDiligentViewport;
}

namespace elm {

	class SocLabWindow;

	class ImGuiSystem {
	public:
		ImGuiSystem();
		~ImGuiSystem();

		ImGuiSystem(const ImGuiSystem&) = delete;
		ImGuiSystem& operator=(const ImGuiSystem&) = delete;

		[[nodiscard]] auto Init(RenderSystem& renderSystem, StringView title) -> elm::EngineResult<void>;
		void BeginFrame(RenderSystem& renderSystem);
		void Render(RenderSystem& renderSystem, const FrameStats& stats);
		void Shutdown();

		// Window management
		void AddWindow(UniquePtr<IImGuiWindow> window);
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
		static void CreateViewport(ImGuiViewport* viewport);
		static void DestroyViewport(ImGuiViewport* viewport);
		static void ResizeViewport(ImGuiViewport* viewport, ImVec2 size);
		static float GetViewportDpiScale(ImGuiViewport* viewport);
		static void RenderViewport(ImGuiViewport* viewport, void* userData);
		static void PresentViewport(ImGuiViewport* viewport, void* userData);

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

		GLFWwindow* m_window{ nullptr };
		UniquePtr<Diligent::ImGuiImplDiligentViewport> m_imGui;
		RenderSystem* m_renderSystem{ nullptr };
		bool m_initialized{ false };
		bool m_glfwInitialized{ false };
		String m_title;
		String m_iniFilePath;
		SavedLabSettings m_savedSettings;

		DockSpaceView m_dockSpace;
		Vector<UniquePtr<IImGuiWindow>> m_windows;
		SocLabWindow* m_socLabWindow{ nullptr };
	};

} // namespace Engine
