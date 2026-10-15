#pragma once

#include "script/ui/ScriptWindowActions.hpp"

#include <array>

struct ImGuiInputTextCallbackData;

namespace elm::script {

    class ScriptConsole;

    /// The Console tab of the Scripts window: the output, the input line with its history and Tab
    /// completion. A line runs in the ScriptConsole; a line starting with '/' is a command for the window.
    /// Owner: ScriptWindow, thread: update (UI).
    class ConsolePanel {
    public:
        /// Gets a command line without its '/'.
        using CommandHandler = std::function<void(StringView command)>;

        ConsolePanel(ScriptConsole& console, CommandHandler onCommand);

        void Draw();
        void Print(ConsoleLineKind kind, StringView text);
        void Clear() noexcept { m_lines.clear(); }
        void ScrollToBottom() noexcept { m_scrollToBottom = true; }
        /// Appends text to the input line and focuses it.
        void InsertIntoInput(StringView text);

    private:
        struct Line {
            ConsoleLineKind kind { ConsoleLineKind::Output };
            String text;
        };

        void DrawOutput();
        void Submit();
        void Complete(ImGuiInputTextCallbackData& data);
        void BrowseHistory(ImGuiInputTextCallbackData& data);
        static int InputCallback(ImGuiInputTextCallbackData* data);

        ScriptConsole& m_console;
        CommandHandler m_onCommand;
        Deque<Line> m_lines;
        Vector<String> m_history;
        /// Position while browsing the history with the arrows, -1 = a new line.
        int m_historyPosition { -1 };
        std::array<char, 4096> m_input { };
        bool m_scrollToBottom { false };
        bool m_focusInput { false };
    };

} // namespace elm::script
