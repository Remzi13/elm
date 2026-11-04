#pragma once

namespace elm {

    namespace graphics {
        class DebugDraw;
    }

    namespace script {
        struct PlayRequests;
    }

    /// What the toolbar works with; Editor gives it once, and it all outlives the UI.
    struct EditorToolbarDesc {
        /// Play and Stop.
        const script::PlayRequests* play { nullptr };
        /// The channels of the Debug menu.
        graphics::DebugDraw* debugDraw { nullptr };
    };

    /// The controls of the main menu bar that are not menus of a file: the Debug menu, Play and Stop (F5 plays the
    /// project, Shift+F5 the edited scene, either stops). What is not set is not shown.
    /// Owner: ImGuiSystem, thread: update (inside the ImGui frame).
    class EditorToolbar {
    public:
        void SetDesc(const EditorToolbarDesc& desc) { m_desc = desc; }

        /// Inside the menu bar.
        void Draw();

    private:
        void DrawDebugMenu();
        void DrawPlayControls();

        EditorToolbarDesc m_desc;
    };

} // namespace elm
