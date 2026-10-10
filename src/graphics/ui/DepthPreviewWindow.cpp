#include "graphics/ui/DepthPreviewWindow.hpp"

#include "render/RenderSystem.hpp"

#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "imgui.h"

namespace elm {

	namespace {
		ImTextureID ToTextureId(render::TextureHandle texture)
		{
		    if (!texture.IsValid())
		        return nullptr;
		    // Real texture views are aligned pointers, so the lowest bit marks an engine handle
		    const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
		    return reinterpret_cast<ImTextureID>(value);
		}
	}

	void DepthPreviewWindow::Render(ImGuiWindowContext& context) {
		auto& renderSystem = context.renderSystem;
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(renderSystem.GetSize().width) - 460.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(450.0f, 350.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		if (!m_cullingSystem) {
			ImGui::TextDisabled("No Occlusion Culling System connected.");
			ImGui::End();
			return;
		}

		const uint32_t depthWidth = m_cullingSystem->GetWidth();
		const uint32_t depthHeight = m_cullingSystem->GetHeight();
		ImGui::Text("Resolution: %ux%u", depthWidth, depthHeight);
		ImGui::SameLine();
		auto falseColor = m_settings.Get<bool>(Settings::Category::Render,
			OcclusionCullingSystem::DepthFalseColorSetting);
		if (ImGui::Checkbox("False Color (Heatmap)", &falseColor)) {
			m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::DepthFalseColorSetting, falseColor);
		}
		
		const ImTextureID texture = ToTextureId(m_cullingSystem->GetDepthPreviewTexture().GetHandle());
		
		if (texture) {
			const float aspect = static_cast<float>(depthWidth) / static_cast<float>(depthHeight);
			const float width = ImGui::GetContentRegionAvail().x;
			const float height = width / aspect;
			const ImVec2 imagePos = ImGui::GetCursorScreenPos();
			ImGui::Image(texture, ImVec2(width, height));
			if (ImGui::IsItemHovered()) {
				const ImVec2 mouse = ImGui::GetMousePos();
				const uint32_t px = static_cast<uint32_t>(((mouse.x - imagePos.x) / width) * depthWidth);
				const uint32_t py = static_cast<uint32_t>(((mouse.y - imagePos.y) / height) * depthHeight);
				if (px < depthWidth && py < depthHeight) {
					ImGui::BeginTooltip();
					ImGui::Text("Pixel: (%u, %u)", px, py);
					ImGui::Text("Normalized Depth: %.4f", m_cullingSystem->GetDepth(px, py));
					ImGui::EndTooltip();
				}
			}
		}
		ImGui::End();
	}

} // namespace elm
