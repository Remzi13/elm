#include "editor/ui/SceneEditShortcuts.hpp"

#include "editor/SceneEdits.hpp"

#include "imgui.h"

namespace elm {

	void HandleSceneEditShortcuts(SceneEdits& edits)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput || !io.KeyCtrl)
			return;
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (io.KeyShift)
				(void)edits.Redo();
			else
				(void)edits.Undo();
		} else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			(void)edits.Redo();
		}
	}

} // namespace elm
