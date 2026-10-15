#include "script/ui/ConsolePanel.hpp"

#include "script/console/ScriptConsole.hpp"
#include "script/ui/ScriptWindowStyle.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace elm::script {

    namespace {

        constexpr size_t MaxConsoleLines = 2000;
        constexpr size_t MaxHistory = 200;

        StringView Trim(StringView text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
                text.remove_suffix(1);
            return text;
        }

        /// Longest common start of the completions.
        String CommonPrefix(const Vector<String>& items)
        {
            String prefix = items.front();
            for (const auto& item : items) {
                size_t length = 0;
                while (length < prefix.size() && length < item.size() && prefix[length] == item[length])
                    ++length;
                prefix.resize(length);
            }
            return prefix;
        }

        const ImVec4& ColorOf(ConsoleLineKind kind)
        {
            switch (kind) {
            case ConsoleLineKind::Command: return style::CommandColor;
            case ConsoleLineKind::Error: return style::ErrorColor;
            case ConsoleLineKind::Info: return style::InfoColor;
            case ConsoleLineKind::Output: break;
            }
            return style::OutputColor;
        }

        void Replace(ImGuiInputTextCallbackData& data, StringView text)
        {
            data.DeleteChars(0, data.BufTextLen);
            data.InsertChars(0, text.data(), text.data() + text.size());
        }

    } // namespace

    ConsolePanel::ConsolePanel(ScriptConsole& console, CommandHandler onCommand)
        : m_console(console)
        , m_onCommand(std::move(onCommand))
    {
    }

    void ConsolePanel::Draw()
    {
        DrawOutput();

        if (m_focusInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory
            | ImGuiInputTextFlags_CallbackCompletion;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Run").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
        const bool entered = ImGui::InputTextWithHint("##input", "Lua, e.g. elm.scene.count()", m_input.data(), m_input.size(), flags,
            &ConsolePanel::InputCallback, this);
        ImGui::SameLine();
        const bool run = ImGui::Button("Run");
        if (entered || run) {
            Submit();
            m_focusInput = true;
        }
        ImGui::TextDisabled("Enter run | Tab complete | Up/Down history | /help");
    }

    void ConsolePanel::DrawOutput()
    {
        const float inputHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("ConsoleOutput", ImVec2(0.0f, -inputHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            if (ImGui::BeginPopupContextWindow()) {
                if (ImGui::MenuItem("Clear"))
                    m_lines.clear();
                if (ImGui::MenuItem("Copy all")) {
                    String all;
                    for (const auto& line : m_lines)
                        all += line.text + "\n";
                    ImGui::SetClipboardText(all.c_str());
                }
                ImGui::EndPopup();
            }

            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
            for (const auto& line : m_lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, ColorOf(line.kind));
                ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
                ImGui::PopStyleColor();
            }
            if (m_scrollToBottom || atBottom)
                ImGui::SetScrollHereY(1.0f);
            m_scrollToBottom = false;
        }
        ImGui::EndChild();
    }

    void ConsolePanel::Print(ConsoleLineKind kind, StringView text)
    {
        // One console line per text line, so long outputs scroll and copy naturally
        size_t begin = 0;
        while (begin <= text.size()) {
            const size_t end = (std::min)(text.find('\n', begin), text.size());
            m_lines.push_back({ kind, String(text.substr(begin, end - begin)) });
            begin = end + 1;
        }
        while (m_lines.size() > MaxConsoleLines)
            m_lines.pop_front();
    }

    void ConsolePanel::InsertIntoInput(StringView text)
    {
        const size_t length = std::strlen(m_input.data());
        const size_t count = (std::min)(text.size(), m_input.size() - 1 - length);
        std::memcpy(m_input.data() + length, text.data(), count);
        m_input[length + count] = '\0';
        m_focusInput = true;
    }

    void ConsolePanel::Submit()
    {
        const String line(Trim(m_input.data()));
        m_input.fill('\0');
        m_historyPosition = -1;
        if (line.empty())
            return;

        // Repeated lines are stored once, the latest at the end
        m_history.erase(std::remove(m_history.begin(), m_history.end(), line), m_history.end());
        m_history.push_back(line);
        if (m_history.size() > MaxHistory)
            m_history.erase(m_history.begin());

        Print(ConsoleLineKind::Command, "> " + line);
        m_scrollToBottom = true;
        if (line.starts_with("/")) {
            m_onCommand(StringView(line).substr(1));
            return;
        }
        if (auto result = m_console.Execute(line)) {
            if (!result->empty())
                Print(ConsoleLineKind::Output, *result);
        } else {
            Print(ConsoleLineKind::Error, result.error().message);
        }
    }

    void ConsolePanel::BrowseHistory(ImGuiInputTextCallbackData& data)
    {
        if (m_history.empty())
            return;
        if (data.EventKey == ImGuiKey_UpArrow)
            m_historyPosition = m_historyPosition < 0 ? static_cast<int>(m_history.size()) - 1 : (std::max)(m_historyPosition - 1, 0);
        else if (data.EventKey == ImGuiKey_DownArrow && m_historyPosition >= 0)
            m_historyPosition = m_historyPosition + 1 < static_cast<int>(m_history.size()) ? m_historyPosition + 1 : -1;
        Replace(data, m_historyPosition < 0 ? StringView() : StringView(m_history[static_cast<size_t>(m_historyPosition)]));
    }

    void ConsolePanel::Complete(ImGuiInputTextCallbackData& data)
    {
        // Complete the text up to the cursor; the rest of the line stays after it
        const StringView before(data.Buf, static_cast<size_t>(data.CursorPos));
        const String after(data.Buf + data.CursorPos);
        const auto completions = m_console.Complete(before);
        if (completions.empty())
            return;
        const String completed = completions.size() == 1 ? completions.front() : CommonPrefix(completions);
        if (completions.size() > 1) {
            String list;
            for (const auto& completion : completions) {
                const size_t separator = completion.find_last_of(".: (=,");
                list += (list.empty() ? "" : "  ") + completion.substr(separator == String::npos ? 0 : separator + 1);
            }
            Print(ConsoleLineKind::Info, list);
            m_scrollToBottom = true;
        }
        Replace(data, completed + after);
        data.CursorPos = static_cast<int>(completed.size());
    }

    int ConsolePanel::InputCallback(ImGuiInputTextCallbackData* data)
    {
        auto& panel = *static_cast<ConsolePanel*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
            panel.BrowseHistory(*data);
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
            panel.Complete(*data);
        return 0;
    }

} // namespace elm::script
