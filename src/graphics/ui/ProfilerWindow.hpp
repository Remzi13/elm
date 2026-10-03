#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

#include <array>
#include <cstdint>

namespace elm {

	class ProfilerWindow final : public IImGuiWindow {
	public:
		ProfilerWindow(Settings& settings) : IImGuiWindow(settings) {}

		[[nodiscard]] StringView GetName() const override { return "Profiler"; }
		void Render(RenderSystem& renderSystem, Scene& scene, const Camera& camera, const FrameStats& stats) override;

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
	};

} // namespace elm
