#pragma once

#include "core/Error.hpp"

#include "graphics/ImGuiRenderer.hpp"

#include "graphics/ui/DockSpaceView.hpp"
#include "graphics/ui/IImGuiWindow.hpp"

#include "imgui.h"

#include <memory>
#include <span>

namespace elm {

	class SocLabWindow;

	class ImGuiSystem {
	public:
		ImGuiSystem();
		~ImGuiSystem();

		ImGuiSystem(const ImGuiSystem&) = delete;
		ImGuiSystem& operator=(const ImGuiSystem&) = delete;

		[[nodiscard]] auto Init(const ImGuiRenderContext& renderContext, StringView title) -> elm::EngineResult<void>;
		void Update(const ImGuiUpdateContext& updateContext, ImGuiConfig& config);
		void Render();
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

		ImGuiRenderer m_renderer;
		String m_iniFilePath;
		SavedLabSettings m_savedSettings;
		ImGuiConfig m_uiConfig;
		bool m_pendingSettings{ false };

		DockSpaceView m_dockSpace;
		Vector<UniquePtr<IImGuiWindow>> m_windows;
		SocLabWindow* m_socLabWindow{ nullptr };
	};

} // namespace Engine
