#include "graphics/ui/EngineViewportWindow.hpp"
#include "graphics/RenderSystem.hpp"

#include "imgui.h"

namespace elm {

	void EngineViewportWindow::Update(const ImGuiUpdateContext& context, ImGuiConfig&) {
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		if (auto* texture = context.engineViewport) {
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float aspect = static_cast<float>(context.engineViewportWidth) /
				static_cast<float>(context.engineViewportHeight);
			ImVec2 size = available;
			if (size.x / aspect < size.y) {
				size.y = size.x / aspect;
			}
			else {
				size.x = size.y * aspect;
			}
			ImGui::Image(reinterpret_cast<ImTextureID>(texture->GetNativeHandle()), size);
		}
		ImGui::End();
	}

} // namespace Engine
