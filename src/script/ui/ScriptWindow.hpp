#pragma once

#include "graphics/ui/IImGuiWindow.hpp"

#include "script/ScriptSystem.hpp"
#include "script/ui/ScriptEditor.hpp"

#include <array>

struct ImGuiInputTextCallbackData;

namespace elm::script {

    /// Editor window for scripts:
    ///  - Console: runs Lua lines against the live engine (elm.* is the engine API), with history,
    ///    Tab completion and /commands (/help);
    ///  - Editor: edits the script files, Ctrl+S saves and reloads the module;
    ///  - Modules: loaded modules, their state and errors, reload buttons;
    ///  - API: everything the bindings expose, a click puts it into the console.
    /// The toolbar shows the main module state, rebuilds the scene (main.build_scene) and
    /// controls hot reload and restarts.
    class ScriptWindow final : public IImGuiWindow {
    public:
        ScriptWindow(Settings& settings, ScriptSystem& scripts);

        [[nodiscard]] StringView GetName() const override { return "Scripts"; }
        void Render(ImGuiWindowContext& context) override;

    private:
        enum class LineKind { Command, Output, Error, Info };

        struct ConsoleLine {
            LineKind kind;
            String text;
        };

        void DrawToolbar();
        void DrawConsole();
        void DrawModules();
        void DrawApi();

        void Submit();
        void RunCommand(StringView command);
        void Print(LineKind kind, StringView text);
        void InsertIntoInput(StringView text);
        void RebuildScene();
        /// Opens a file in the Editor tab and switches to it.
        void Edit(StringView module, int line = 0);
        void EditLocation(StringView message);
        static int InputCallback(ImGuiInputTextCallbackData* data);

        ScriptSystem& m_scripts;
        ScriptEditor m_editor;

        Deque<ConsoleLine> m_lines;
        Vector<String> m_history;
        /// Position while browsing the history with the arrows, -1 = a new line.
        int m_historyPosition { -1 };
        std::array<char, 4096> m_input { };
        std::array<char, 128> m_apiFilter { };
        bool m_scrollToBottom { false };
        bool m_focusInput { false };
        bool m_selectConsole { false };
        bool m_selectEditor { false };
    };

} // namespace elm::script
