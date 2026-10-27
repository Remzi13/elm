#include "graphics/ui/SocLabWindow.hpp"

#include "graphics/culling/OcclusionCullingSystem.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace elm {

	namespace {
		static constexpr std::array<uint32_t, 5> ResolutionWidths = { 64, 128, 256, 320, 512 };
		static constexpr std::array<uint32_t, 5> ResolutionHeights = { 36, 72, 144, 180, 288 };

		ImTextureID ToTextureId(render::TextureHandle texture)
		{
			if (!texture.IsValid())
				return nullptr;
			// Real texture views are aligned pointers, so the lowest bit marks an engine handle
			const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
			return reinterpret_cast<ImTextureID>(value);
		}
	}

	void SocLabWindow::SetResolutionIndex(int index) noexcept {
		m_currentResolution = std::clamp(index, 0, static_cast<int>(ResolutionWidths.size()) - 1);
	}

	void SocLabWindow::ApplyResolution() {
		const uint32_t width = ResolutionWidths[m_currentResolution];
		const uint32_t height = ResolutionHeights[m_currentResolution];
		m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::ResolutionWidthSetting, width);
		m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::ResolutionHeightSetting, height);
	}

	void SocLabWindow::Render(ImGuiWindowContext& /*context*/) {
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(480.0f, 680.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Software Depth Buffer Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (!m_cullingSystem) {
				ImGui::TextDisabled("No Occlusion Culling System connected.");
			} else {
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
				if (texture && depthHeight > 0) {
					const float aspect = static_cast<float>(depthWidth) / static_cast<float>(depthHeight);
					const float width = ImGui::GetContentRegionAvail().x;
					const float height = width / aspect;
					const ImVec2 imagePos = ImGui::GetCursorScreenPos();
					ImGui::Image(texture, ImVec2(width, height));
					if (ImGui::IsItemHovered()) {
						const ImVec2 mouse = ImGui::GetMousePos();
						const uint32_t px = static_cast<uint32_t>(((mouse.x - imagePos.x) / width) * static_cast<float>(depthWidth));
						const uint32_t py = static_cast<uint32_t>(((mouse.y - imagePos.y) / height) * static_cast<float>(depthHeight));
						if (px < depthWidth && py < depthHeight) {
							ImGui::BeginTooltip();
							ImGui::Text("Pixel: (%u, %u)", px, py);
							ImGui::Text("Normalized Depth: %.4f", m_cullingSystem->GetDepth(px, py));
							ImGui::EndTooltip();
						}
					}
				}
			}
		}

		if (ImGui::CollapsingHeader("Culling Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto enableFrustumCulling = m_settings.Get<bool>(Settings::Category::Render,
				OcclusionCullingSystem::EnableFrustumCullingSetting);
			if (ImGui::Checkbox("Enable Frustum Culling", &enableFrustumCulling)) {
				m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::EnableFrustumCullingSetting,
					enableFrustumCulling);
			}
			auto enableOcclusionCulling = m_settings.Get<bool>(Settings::Category::Render,
				OcclusionCullingSystem::EnableOcclusionCullingSetting);
			if (ImGui::Checkbox("Enable Software Occlusion Culling", &enableOcclusionCulling)) {
				m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::EnableOcclusionCullingSetting,
					enableOcclusionCulling);
			}
			auto depthBias = m_settings.Get<float>(Settings::Category::Render, OcclusionCullingSystem::DepthBiasSetting);
			if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.0f, 0.01f, "%.4f")) {
				m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::DepthBiasSetting, depthBias);
			}
			const uint32_t currentWidth =
				m_settings.Get<uint32_t>(Settings::Category::Render, OcclusionCullingSystem::ResolutionWidthSetting);
			const uint32_t currentHeight =
				m_settings.Get<uint32_t>(Settings::Category::Render, OcclusionCullingSystem::ResolutionHeightSetting);
			for (size_t index = 0; index < ResolutionWidths.size(); ++index) {
				if (ResolutionWidths[index] == currentWidth && ResolutionHeights[index] == currentHeight) {
					m_currentResolution = static_cast<int>(index);
					break;
				}
			}
			const char* resolutions[] = { "64x36", "128x72", "256x144 (Recommended)", "320x180", "512x288" };
			if (ImGui::Combo("SOC Buffer Res", &m_currentResolution, resolutions, IM_ARRAYSIZE(resolutions))) {
				ApplyResolution();
			}
		}

		if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
			int mode = static_cast<int>(m_settings.Get<uint32_t>(Settings::Category::Render,
				OcclusionCullingSystem::VisualModeSetting));
			bool modeChanged = false;
			if (ImGui::RadioButton("Hide Culled (Draw Visible Only)", &mode, 0)) modeChanged = true;
			if (ImGui::RadioButton("Highlight Culled (Red Ghost)", &mode, 1)) modeChanged = true;
			if (ImGui::RadioButton("Occluders Only", &mode, 2)) modeChanged = true;
			if (modeChanged) {
				m_settings.Set(Settings::Category::Render, OcclusionCullingSystem::VisualModeSetting,
					static_cast<uint32_t>(mode));
			}
		}

		if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (m_cullingSystem) {
				const auto& s = m_cullingSystem->GetStats();
				ImGui::Text("Total Objects:     %u", s.totalObjects);
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", s.visibleCount);
				ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", s.frustumCulledCount);
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", s.occlusionCulledCount);
				ImGui::Spacing();
				char overlay[32];
				std::snprintf(overlay, sizeof(overlay), "Culled: %.1f%%", s.cullingRatioPercent);
				ImGui::ProgressBar(s.cullingRatioPercent / 100.0f, ImVec2(-1.0f, 0.0f), overlay);
				ImGui::Separator();
				ImGui::Text("CPU Rasterize Time: %.1f us (%.3f ms)", s.rasterizeTimeUs, s.rasterizeTimeUs / 1000.0f);
				ImGui::Text("CPU Query / Test:   %.1f us (%.3f ms)", s.queryTimeUs, s.queryTimeUs / 1000.0f);
				ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time: %.1f us (%.3f ms)",
					s.totalCullingTimeUs, s.totalCullingTimeUs / 1000.0f);
			} else {
				ImGui::TextDisabled("No Occlusion Culling System connected.");
			}
		}

		if (ImGui::CollapsingHeader("Camera & Controls")) {
			ImGui::Separator();
			ImGui::TextDisabled("Controls:");
			ImGui::BulletText("Right Mouse Button + Drag: Look around");
			ImGui::BulletText("W / A / S / D: Move forward / left / back / right");
			ImGui::BulletText("E / Q: Move Up / Down");
			ImGui::BulletText("Left Shift: Sprint (2.5x speed)");
		}
		ImGui::End();
	}

} // namespace elm
