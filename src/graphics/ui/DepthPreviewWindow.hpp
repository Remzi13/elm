#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

namespace elm {

	class OcclusionCullingSystem;

	class DepthPreviewWindow final : public IImGuiWindow {
	public:
		DepthPreviewWindow(Settings& settings, const OcclusionCullingSystem* cullingSystem = nullptr)
			: IImGuiWindow(settings), m_cullingSystem(cullingSystem) {}

		void SetCullingSystem(const OcclusionCullingSystem* cullingSystem) noexcept { m_cullingSystem = cullingSystem; }

		[[nodiscard]] StringView GetName() const override { return "Software Depth Buffer Viewport"; }
		void Render(RenderSystem& renderSystem, Scene& scene, const FrameStats& stats) override;

	private:
		const OcclusionCullingSystem* m_cullingSystem{ nullptr };
	};

} // namespace elm
