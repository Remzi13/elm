#include "graphics/ui/SocLabWindow.hpp"
#include "graphics/RenderSystem.hpp"

#include "imgui.h"

#include <algorithm>

namespace elm {

	void SocLabWindow::SetResolutionIndex(int index) noexcept {
		m_currentResolution = std::clamp(index, 0, static_cast<int>(ResolutionWidths.size()) - 1);
	}

	void SocLabWindow::ApplyResolution(RenderSystem& renderSystem) {
		const uint32_t width = ResolutionWidths[m_currentResolution];
		const uint32_t height = ResolutionHeights[m_currentResolution];
		renderSystem.GetCullingSystem().SetResolution(width, height);
		renderSystem.CreateDepthPreviewTexture(width, height);
	}

	void SocLabWindow::Render(RenderSystem& renderSystem, const FrameStats& stats) {
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(440.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "C++23 Vulkan SOC Testbed");
		ImGui::Text("FPS: %.1f | Frame: %.2f ms", stats.fps, stats.deltaTimeMs);
		ImGui::Separator();

		if (ImGui::CollapsingHeader("Scene Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
			const char* presets[] = { "The Great Wall & City Grid", "Rooms & Corridors", "Physics Barrier Sandbox" };
			int preset = static_cast<int>(renderSystem.GetCurrentPreset());
			if (ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
				renderSystem.SetCurrentPreset(static_cast<ScenePreset>(preset));
				renderSystem.RebuildScene();
				//ImGui::MarkIniSettingsDirty();
			}
			int targetInstances = renderSystem.GetTargetInstanceCount();
			if (ImGui::SliderInt("Instances", &targetInstances, 100, 10000)) {
				renderSystem.SetTargetInstanceCount(targetInstances);
				renderSystem.RebuildScene();
				//ImGui::MarkIniSettingsDirty();
			}
		}

		if (ImGui::CollapsingHeader("Culling Algorithms", ImGuiTreeNodeFlags_DefaultOpen)) {
			auto& culling = renderSystem.GetCullingSystem();
			if (ImGui::Checkbox("Enable Frustum Culling", &culling.enableFrustumCulling)) {
				//ImGui::MarkIniSettingsDirty();
			}
			if (ImGui::Checkbox("Enable Software Occlusion Culling", &culling.enableOcclusionCulling)) {
				//ImGui::MarkIniSettingsDirty();
			}
			bool freeze = renderSystem.GetCamera().IsCullingFrozen();
			if (ImGui::Checkbox("Freeze Culling Camera", &freeze)) {
				renderSystem.GetCamera().SetFreezeCulling(freeze);
			}
			if (freeze) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[FROZEN]");
			}
			if (ImGui::SliderFloat("Depth Bias", &culling.depthBias, 0.0f, 0.01f, "%.4f")) {
				//ImGui::MarkIniSettingsDirty();
			}
			const char* resolutions[] = { "64x36", "128x72", "256x144 (Recommended)", "320x180", "512x288" };
			if (ImGui::Combo("SOC Buffer Res", &m_currentResolution, resolutions, IM_ARRAYSIZE(resolutions))) {
				ApplyResolution(renderSystem);
				//ImGui::MarkIniSettingsDirty();
			}
		}

		if (ImGui::CollapsingHeader("Visualization Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
			int mode = static_cast<int>(renderSystem.GetCullingSystem().visualMode);
			bool modeChanged = false;
			if (ImGui::RadioButton("Hide Culled (Draw Visible Only)", &mode, 0)) modeChanged = true;
			if (ImGui::RadioButton("Highlight Culled (Red Ghost)", &mode, 1)) modeChanged = true;
			if (ImGui::RadioButton("Occluders Only", &mode, 2)) modeChanged = true;
			if (modeChanged) {
				renderSystem.GetCullingSystem().visualMode = static_cast<VisualMode>(mode);
				//ImGui::MarkIniSettingsDirty();
			}
		}

		if (ImGui::CollapsingHeader("Real-Time Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
			const auto& s = renderSystem.GetCullingSystem().GetStats();
			ImGui::Text("Total Objects:     %u", s.totalObjects);
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Visible Rendered:  %u", s.visibleCount);
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Frustum Culled:    %u", s.frustumCulledCount);
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Occlusion Culled:  %u", s.occlusionCulledCount);
			ImGui::Spacing();
			ImGui::ProgressBar(s.cullingRatioPercent / 100.0f, ImVec2(-1, 0), "");
			ImGui::SameLine();
			ImGui::Text("Culled: %.1f%%", s.cullingRatioPercent);
			ImGui::Separator();
			ImGui::Text("CPU Rasterize Time: %.1f us (%.3f ms)", s.rasterizeTimeUs, s.rasterizeTimeUs / 1000.0f);
			ImGui::Text("CPU Query / Test:   %.1f us (%.3f ms)", s.queryTimeUs, s.queryTimeUs / 1000.0f);
			ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Total Culling Time: %.1f us (%.3f ms)",
				s.totalCullingTimeUs, s.totalCullingTimeUs / 1000.0f);
		}

		if (ImGui::CollapsingHeader("Camera & Controls")) {
			const Vector3 pos = renderSystem.GetCamera().GetPosition();
			ImGui::Text("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
			if (ImGui::SliderFloat("Move Speed", &renderSystem.GetCamera().moveSpeed, 5.0f, 50.0f)) {
				//ImGui::MarkIniSettingsDirty();
			}
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
