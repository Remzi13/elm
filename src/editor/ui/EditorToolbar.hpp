#pragma once

#include <functional>

namespace elm {

    namespace script {
        struct PlayRequests;
    }

    /// What the toolbar works with; Editor gives it once, and it all outlives the UI.
    struct EditorToolbarDesc {
        /// Play and Stop.
        const script::PlayRequests* play { nullptr };
        /// Runs the open project in a player process of its own, from its files; nothing without a project.
        std::function<void()> runStandalone;
    };

    /// The controls of the main menu bar that are not menus: in the middle of the bar, every way to start the game. Play (F5) plays the edited scene; the arrow beside it lists Play Scene, Play
    /// Project (Shift+F5) and Run Standalone (Ctrl+F5). In Play there is Stop (F5). What is not set is not shown.
    /// Owner: ImGuiSystem, thread: update (inside the ImGui frame).
    class EditorToolbar {
    public:
        void SetDesc(const EditorToolbarDesc& desc) { m_desc = desc; }

        /// Inside the menu bar.
        void Draw();

    private:
        void DrawStartMenu(const script::PlayRequests& play);

        EditorToolbarDesc m_desc;
    };

} // namespace elm
