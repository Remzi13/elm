#pragma once

#include "core/Std.hpp"

#include "Scene/TestScenes.hpp"
#include "graphics/Settings.hpp"

namespace elm {

	class RenderSystem;
	struct FrameStats;

	class IImGuiWindow {
	public:
		IImGuiWindow(Settings& settings) : m_settings(settings) {}
		virtual ~IImGuiWindow() = default;

		
		virtual void Render(RenderSystem& renderSystem, Scene& scene, const FrameStats& stats) = 0;
		[[nodiscard]] virtual StringView GetName() const = 0;

		[[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
		void SetVisible(bool visible) noexcept { m_visible = visible; }
		bool* GetVisiblePtr() noexcept { return &m_visible; }

	protected:
		Settings& m_settings;
		bool m_visible{ true };
	};

} // namespace Engine
