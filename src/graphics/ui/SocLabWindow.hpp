#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

#include <array>
#include <cstdint>

namespace elm {

	class OcclusionCullingSystem;

	class SocLabWindow final : public IImGuiWindow {
	public:
		SocLabWindow(Settings& settings, const OcclusionCullingSystem* cullingSystem = nullptr)
			: IImGuiWindow(settings), m_cullingSystem(cullingSystem) {}

		[[nodiscard]] StringView GetName() const override { return "Software Occlusion Culling Lab"; }
		void Render(ImGuiWindowContext& context) override;

		void SetCullingSystem(const OcclusionCullingSystem* cullingSystem) noexcept { m_cullingSystem = cullingSystem; }

		[[nodiscard]] int GetResolutionIndex() const noexcept { return m_currentResolution; }
		void SetResolutionIndex(int index) noexcept;

	private:
		void ApplyResolution();

	private:
		const OcclusionCullingSystem* m_cullingSystem{ nullptr };
		int m_currentResolution{ 2 };
	};

} // namespace elm
