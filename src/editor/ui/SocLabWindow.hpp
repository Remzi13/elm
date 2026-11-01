#pragma once

#include "core/Std.hpp"

#include "editor/ui/IImGuiWindow.hpp"

#include <array>
#include <cstdint>

namespace elm {

	class OcclusionCulling;

	class SocLabWindow final : public IImGuiWindow {
	public:
		SocLabWindow(Settings& settings, const OcclusionCulling* culling = nullptr)
			: IImGuiWindow(settings), m_culling(culling) {}

		[[nodiscard]] StringView GetName() const override { return "Software Occlusion Culling Lab"; }
		void Render(render::FrameWriter& frame) override;

		void SetCullingSystem(const OcclusionCulling* culling) noexcept { m_culling = culling; }

		[[nodiscard]] int GetResolutionIndex() const noexcept { return m_currentResolution; }
		void SetResolutionIndex(int index) noexcept;

	private:
		void ApplyResolution();

	private:
		const OcclusionCulling* m_culling{ nullptr };
		int m_currentResolution{ 2 };
	};

} // namespace elm
