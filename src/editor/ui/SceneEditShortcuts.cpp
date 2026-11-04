#include "editor/ui/SceneEditShortcuts.hpp"

#include "editor/SceneEdits.hpp"
#include "editor/ui/EditorServices.hpp"

#include "imgui.h"

namespace elm {

    void HandleSceneEditShortcuts(SceneEdits& edits, const SceneFileRequests& file)
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
        } else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && file.save) {
            file.save();
        }
    }

} // namespace elm
