#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

namespace elm {

	class SettingsWindow final : public IImGuiWindow {
	public:
		using IImGuiWindow::IImGuiWindow;

		[[nodiscard]] StringView GetName() const override { return "Settings"; }
		void Render(ImGuiWindowContext& context) override;

	private:
		String m_status;
		bool m_statusIsError{ false };
	};

} // namespace elm
