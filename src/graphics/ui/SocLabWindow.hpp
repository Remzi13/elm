#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

#include <array>
#include <cstdint>

namespace elm {

	class SocLabWindow final : public IImGuiWindow {
	public:
		SocLabWindow() = default;

		[[nodiscard]] StringView GetName() const override { return "Software Occlusion Culling Lab"; }
		void Render(RenderSystem& renderSystem, const FrameStats& stats) override;

		[[nodiscard]] int GetResolutionIndex() const noexcept { return m_currentResolution; }
		void SetResolutionIndex(int index) noexcept;
		void ApplyResolution(RenderSystem& renderSystem);

		static constexpr std::array<uint32_t, 5> ResolutionWidths = { 64, 128, 256, 320, 512 };
		static constexpr std::array<uint32_t, 5> ResolutionHeights = { 36, 72, 144, 180, 288 };

	private:
		int m_currentResolution{ 2 };
	};

} // namespace Engine
