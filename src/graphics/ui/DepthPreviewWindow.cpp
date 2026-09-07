#include "graphics/ui/DepthPreviewWindow.hpp"
#include "graphics/RenderSystem.hpp"

#include "imgui.h"

namespace elm {

	void DepthPreviewWindow::Update(const ImGuiUpdateContext& context, ImGuiConfig& config) {
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(context.width) - 460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(450.0f, 350.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		const uint32_t depthWidth = context.depthPreviewWidth;
		const uint32_t depthHeight = context.depthPreviewHeight;
		ImGui::Text("Resolution: %ux%u", depthWidth, depthHeight);
		ImGui::SameLine();
		bool falseColor = config.depthPreviewFalseColor;
		if (ImGui::Checkbox("False Color (Heatmap)", &falseColor)) {
			config.depthPreviewFalseColor = falseColor;
			//ImGui::MarkIniSettingsDirty();
		}
		if (auto* texture = context.depthPreview) {
			const float aspect = static_cast<float>(depthWidth) / static_cast<float>(depthHeight);
			const float width = ImGui::GetContentRegionAvail().x;
			const float height = width / aspect;
			const ImVec2 imagePos = ImGui::GetCursorScreenPos();
			ImGui::Image(reinterpret_cast<ImTextureID>(texture->GetNativeHandle()), ImVec2(width, height));
			if (ImGui::IsItemHovered()) {
				const ImVec2 mouse = ImGui::GetMousePos();
				const uint32_t px = static_cast<uint32_t>(((mouse.x - imagePos.x) / width) * depthWidth);
				const uint32_t py = static_cast<uint32_t>(((mouse.y - imagePos.y) / height) * depthHeight);
				if (px < depthWidth && py < depthHeight) {
					ImGui::BeginTooltip();
					ImGui::Text("Pixel: (%u, %u)", px, py);
					const float depth = context.depthBuffer ? context.depthBuffer->GetDepth(px, py) : 0.0f;
					ImGui::Text("Normalized Depth: %.4f", depth);
					ImGui::EndTooltip();
				}
			}
		}
		ImGui::End();
	}

} // namespace Engine
