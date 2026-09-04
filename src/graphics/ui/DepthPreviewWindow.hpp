#pragma once

#include "core/Std.hpp"

#include "graphics/ui/IImGuiWindow.hpp"

namespace elm {

	class DepthPreviewWindow final : public IImGuiWindow {
	public:
		DepthPreviewWindow() = default;

		[[nodiscard]] StringView GetName() const override { return "Software Depth Buffer Viewport"; }
		void Render(RenderSystem& renderSystem, const FrameStats& stats) override;
	};

} // namespace Engine
