#pragma once

#include "core/Std.hpp"

#include "editor/ui/IImGuiWindow.hpp"

#include <array>
#include <cstdint>

namespace elm {

	struct FrameStats;

	namespace render {
		class Render;
	}

	/// What the profiler shows; they outlive it (Editor).
	struct ProfilerDeps {
		/// Draw and memory statistics of rendering.
		const render::Render& render;
		/// The frame rate, updated by Editor every frame.
		const FrameStats& stats;
	};

	/// Frame rate history, rendering and memory statistics. Owner: ImGuiSystem, thread: update (UI).
	class ProfilerWindow final : public IImGuiWindow {
	public:
		ProfilerWindow(Settings& settings, const ProfilerDeps& deps);

		[[nodiscard]] StringView GetName() const override { return "Profiler"; }
		void Render(render::FrameWriter& frame) override;

	private:
		static constexpr size_t kHistorySize = 256;

		std::array<float, kHistorySize> m_fpsHistory{};
		std::array<float, kHistorySize> m_frameTimeHistory{};
		size_t m_historyOffset{ 0 };

		float m_fpsMin{ 0.0f };
		float m_fpsMax{ 0.0f };
		float m_fpsAvg{ 0.0f };

		float m_frameTimeMin{ 0.0f };
		float m_frameTimeMax{ 0.0f };
		float m_frameTimeAvg{ 0.0f };

		void UpdateHistory(float fps, float frameTimeMs);

		ProfilerDeps m_deps;
	};

} // namespace elm
