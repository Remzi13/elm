#include "graphics/ui/ProfilerWindow.hpp"
#include "graphics/RenderSystem.hpp"

#include "imgui.h"

#include <algorithm>
#include <numeric>

namespace elm {

	void ProfilerWindow::UpdateHistory(float fps, float frameTimeMs) {
		m_fpsHistory[m_historyOffset] = fps;
		m_frameTimeHistory[m_historyOffset] = frameTimeMs;
		m_historyOffset = (m_historyOffset + 1) % kHistorySize;

		// Compute stats over the filled history
		float fpsSum = 0.0f;
		float ftSum = 0.0f;
		m_fpsMin = m_fpsHistory[0];
		m_fpsMax = m_fpsHistory[0];
		m_frameTimeMin = m_frameTimeHistory[0];
		m_frameTimeMax = m_frameTimeHistory[0];

		for (size_t i = 0; i < kHistorySize; ++i) {
			fpsSum += m_fpsHistory[i];
			ftSum += m_frameTimeHistory[i];
			m_fpsMin = std::min(m_fpsMin, m_fpsHistory[i]);
			m_fpsMax = std::max(m_fpsMax, m_fpsHistory[i]);
			m_frameTimeMin = std::min(m_frameTimeMin, m_frameTimeHistory[i]);
			m_frameTimeMax = std::max(m_frameTimeMax, m_frameTimeHistory[i]);
		}
		m_fpsAvg = fpsSum / static_cast<float>(kHistorySize);
		m_frameTimeAvg = ftSum / static_cast<float>(kHistorySize);
	}

	void ProfilerWindow::Render(ImGuiWindowContext& context) {
		auto& renderSystem = context.renderSystem;
		const auto& stats = context.stats;
		if (!m_visible) return;

		ImGui::SetNextWindowPos(ImVec2(10.0f, 580.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(440.0f, 380.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetName().data(), &m_visible)) {
			ImGui::End();
			return;
		}

		UpdateHistory(stats.fps, stats.deltaTimeMs);

		// --- FPS section ---
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Frame Rate");
		ImGui::Separator();

		ImGui::Text("FPS: %.1f", stats.fps);
		ImGui::SameLine(200.0f);
		ImGui::Text("Frame: %.2f ms", stats.deltaTimeMs);

		// Color the FPS value
		ImVec4 fpsColor;
		if (stats.fps >= 60.0f) {
			fpsColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); // green
		} else if (stats.fps >= 30.0f) {
			fpsColor = ImVec4(1.0f, 0.9f, 0.3f, 1.0f); // yellow
		} else {
			fpsColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // red
		}

		ImGui::TextColored(fpsColor, "Avg: %.1f  Min: %.1f  Max: %.1f", m_fpsAvg, m_fpsMin, m_fpsMax);

		// FPS graph
		{
			char overlay[64];
			snprintf(overlay, sizeof(overlay), "%.1f FPS", stats.fps);

			// Build ordered array for the plot (oldest → newest)
			float ordered[kHistorySize];
			for (size_t i = 0; i < kHistorySize; ++i) {
				ordered[i] = m_fpsHistory[(m_historyOffset + i) % kHistorySize];
			}
			ImGui::PlotLines("##fps_graph", ordered, static_cast<int>(kHistorySize),
				0, overlay, 0.0f, m_fpsMax * 1.2f, ImVec2(-1, 60));
		}

		ImGui::Spacing();

		// --- Frame time section ---
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Frame Time");
		ImGui::Separator();

		ImGui::TextColored(
			ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
			"Avg: %.2f ms  Min: %.2f ms  Max: %.2f ms",
			m_frameTimeAvg, m_frameTimeMin, m_frameTimeMax);

		// Frame time graph
		{
			char overlay[64];
			snprintf(overlay, sizeof(overlay), "%.2f ms", stats.deltaTimeMs);

			float ordered[kHistorySize];
			for (size_t i = 0; i < kHistorySize; ++i) {
				ordered[i] = m_frameTimeHistory[(m_historyOffset + i) % kHistorySize];
			}
			ImGui::PlotHistogram("##frametime_graph", ordered, static_cast<int>(kHistorySize),
				0, overlay, 0.0f, m_frameTimeMax * 1.2f, ImVec2(-1, 60));
		}

		ImGui::Spacing();

		// --- Memory section ---
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Memory");
		ImGui::Separator();

		const long long allocatedMemoryKb = static_cast<long long>(memory::getStatistic().allocated / 1024);
		const unsigned long long renderCpuMemoryKb = static_cast<unsigned long long>(renderSystem.GetMemAllocated() / 1024);

		const auto formatMemory = [](auto valueKb) -> const char* {
			static char buf[64];
			if (valueKb >= 1024) {
				snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(valueKb) / 1024.0);
			} else {
				snprintf(buf, sizeof(buf), "%lld KB", static_cast<long long>(valueKb));
			}
			return buf;
		};

		ImGui::BulletText("Total Allocated: %s", formatMemory(allocatedMemoryKb));
		ImGui::BulletText("Render CPU:      %s", formatMemory(renderCpuMemoryKb));

		if (allocatedMemoryKb > 0) {
			const float renderFraction = static_cast<float>(renderCpuMemoryKb) / static_cast<float>(allocatedMemoryKb);
			ImGui::ProgressBar(renderFraction, ImVec2(-1, 0), "");
			ImGui::SameLine(0.0f, 5.0f);
			ImGui::Text("Render / Total: %.1f%%", renderFraction * 100.0f);
		}

		ImGui::End();
	}

} // namespace elm
