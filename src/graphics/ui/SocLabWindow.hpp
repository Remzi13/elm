#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

#include "Scene/TestScenes.hpp"

#include <array>
#include <cstdint>

namespace elm {

	class SocLabWindow final : public IImGuiWindow {
	public:
		SocLabWindow(Settings& settings) : IImGuiWindow(settings) {}

		[[nodiscard]] StringView GetName() const override { return "Software Occlusion Culling Lab"; }
		void Render(RenderSystem& renderSystem, Scene& scene, const FrameStats& stats) override;

		[[nodiscard]] int GetResolutionIndex() const noexcept { return m_currentResolution; }
		void SetResolutionIndex(int index) noexcept;
		void ApplyResolution(RenderSystem& renderSystem);		

	private:
		int m_currentResolution{ 2 };
	};

} // namespace Engine
