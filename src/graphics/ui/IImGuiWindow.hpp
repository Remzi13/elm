#pragma once

#include "core/Std.hpp"

namespace elm {

	class RenderSystem;
	struct FrameStats;

	class IImGuiWindow {
	public:
		virtual ~IImGuiWindow() = default;

		virtual void Render(RenderSystem& renderSystem, const FrameStats& stats) = 0;
		[[nodiscard]] virtual StringView GetName() const = 0;

		[[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
		void SetVisible(bool visible) noexcept { m_visible = visible; }
		bool* GetVisiblePtr() noexcept { return &m_visible; }

	protected:
		bool m_visible{ true };
	};

} // namespace Engine
