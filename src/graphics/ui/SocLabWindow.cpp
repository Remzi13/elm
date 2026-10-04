#include "graphics/ui/SocLabWindow.hpp"

#include "imgui.h"

#include <algorithm>

namespace elm {

	namespace {
		static constexpr std::array<uint32_t, 5> ResolutionWidths = { 64, 128, 256, 320, 512 };
		static constexpr std::array<uint32_t, 5> ResolutionHeights = { 36, 72, 144, 180, 288 };
	}

	void SocLabWindow::SetResolutionIndex(int index) noexcept {
		m_currentResolution = std::clamp(index, 0, static_cast<int>(ResolutionWidths.size()) - 1);
	}

	void SocLabWindow::ApplyResolution() {
		const uint32_t width = ResolutionWidths[m_currentResolution];
		const uint32_t height = ResolutionHeights[m_currentResolution];
		m_settings.Set(Settings::Category::Render, CULLING_RESOLUTION_WIDTH, width);
		m_settings.Set(Settings::Category::Render, CULLING_RESOLUTION_HEIGHT, height);
	}

	void SocLabWindow::Render(ImGuiWindowContext& context) {
		auto& scene = context.scene;
		const auto& stats = context.stats;
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(440.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Culling Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) {

			auto enableFrustumCulling = m_settings.Get<bool>(Settings::Category::Render, CULLING_ENABLE_FRUSTUM_CULLING);
			if (ImGui::Checkbox("Enable Frustum Culling", &enableFrustumCulling)) {
				m_settings.Set(Settings::Category::Render, CULLING_ENABLE_FRUSTUM_CULLING, enableFrustumCulling);
			}
			auto enableOcclusionCulling = m_settings.Get<bool>(Settings::Category::Render, CULLING_ENABLE_OCCLUSION_CULLING);
			if (ImGui::Checkbox("Enable Software Occlusion Culling", &enableOcclusionCulling)) {
				m_settings.Set(Settings::Category::Render, CULLING_ENABLE_OCCLUSION_CULLING, enableOcclusionCulling);
			}
			auto depthBias = m_settings.Get<float>(Settings::Category::Render, CULLING_DEPTH_BIAS);
			if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.0f, 0.01f, "%.4f")) {
				m_settings.Set(Settings::Category::Render, CULLING_DEPTH_BIAS, depthBias);
			}
			const char* resolutions[] = { "64x36", "128x72", "256x144 (Recommended)", "320x180", "512x288" };
			if (ImGui::Combo("SOC Buffer Res", &m_currentResolution, resolutions, IM_ARRAYSIZE(resolutions))) {
				ApplyResolution();
			}
		}

		if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
			int mode = m_settings.Get<uint32_t>(Settings::Category::Render, CULLING_VISUAL_MODE);
			bool modeChanged = false;
			if (ImGui::RadioButton("Hide Culled (Draw Visible Only)", &mode, 0)) modeChanged = true;
			if (ImGui::RadioButton("Highlight Culled (Red Ghost)", &mode, 1)) modeChanged = true;
			if (ImGui::RadioButton("Occluders Only", &mode, 2)) modeChanged = true;
			if (modeChanged) {
				m_settings.Set(Settings::Category::Render, CULLING_VISUAL_MODE, static_cast<uint32_t>(mode));
			}
		}

		if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
			//const auto& s = renderSystem.GetCullingSystem().GetStats();
			//ImGui::Text("Total Objects:     %u", s.totalObjects);
			//ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", s.visibleCount);
			//ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", s.frustumCulledCount);
			//ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", s.occlusionCulledCount);
			//ImGui::Spacing();
			//ImGui::ProgressBar(s.cullingRatioPercent / 100.0f, ImVec2(-1, 0), "");
			//ImGui::SameLine();
			//ImGui::Text("Culled: %.1f%%", s.cullingRatioPercent);
			//ImGui::Separator();
			//ImGui::Text("CPU Rasterize Time: %.1f us (%.3f ms)", s.rasterizeTimeUs, s.rasterizeTimeUs / 1000.0f);
			//ImGui::Text("CPU Query / Test:   %.1f us (%.3f ms)", s.queryTimeUs, s.queryTimeUs / 1000.0f);
			//ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time: %.1f us (%.3f ms)",
			//	s.totalCullingTimeUs, s.totalCullingTimeUs / 1000.0f);
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

} // namespace Engine
