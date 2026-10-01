#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/ImGuiSystem.hpp"
#include "graphics/RenderSystem.hpp"

#include "imgui.h"

namespace elm {

	void EngineViewportWindow::Render(RenderSystem& renderSystem, Scene& sceen, const FrameStats&) {
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		if (auto texture = ImGuiSystem::ToTextureId(renderSystem.GetEngineViewportTexture())) {
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const auto* viewport = ImGui::GetWindowViewport();
			const float dpiScale = viewport ? viewport->DpiScale : 1.0f;
			if (available.x > 0.0f && available.y > 0.0f) {
				renderSystem.QueueEngineViewportResize(
					static_cast<uint32_t>(available.x * dpiScale),
					static_cast<uint32_t>(available.y * dpiScale));
				ImGui::Image(texture, available);
			}
		}
		ImGui::End();
	}

} // namespace Engine
