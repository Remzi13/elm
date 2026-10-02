#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include <cstddef>

namespace elm {

	class SceneHierarchyWindow final : public IImGuiWindow {
	public:
		using IImGuiWindow::IImGuiWindow;

		[[nodiscard]] StringView GetName() const override { return "Scene Hierarchy"; }
		void Render(RenderSystem& renderSystem, Scene& scene, const FrameStats& stats) override;

	private:
		size_t m_selectedInstance{ 0 };
	};

} // namespace elm