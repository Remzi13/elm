#include "script/ui/ScriptWindow.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace elm::script {

    namespace {

        constexpr size_t MaxConsoleLines = 2000;
        constexpr size_t MaxHistory = 200;

        const ImVec4 CommandColor { 0.55f, 0.75f, 1.0f, 1.0f };
        const ImVec4 OutputColor { 0.90f, 0.90f, 0.90f, 1.0f };
        const ImVec4 ErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
        const ImVec4 InfoColor { 0.60f, 0.63f, 0.68f, 1.0f };
        const ImVec4 OkColor { 0.40f, 0.85f, 0.50f, 1.0f };
        const ImVec4 WarningColor { 1.0f, 0.78f, 0.35f, 1.0f };

        constexpr const char* HelpText =
            "Enter runs a Lua line against the live engine, an expression prints its value.\n"
            "Tab completes names (elm.sc -> elm.scene), Up/Down walk the history.\n"
            "  elm.scene / elm.models / elm.camera / elm.input / elm.math   engine API (see the API tab)\n"
            "  elm.engine.load_test_scene(1), clear_scene(), save_scene(f), load_scene(f)\n"
            "  elm.script.reload(name), reload_changed(), restart(), persist(key, default)\n"
            "Console commands:\n"
            "  /help  /clear  /modules  /reload [module]  /restart\n"
            "  /scene            rebuild the scene: main.build_scene()\n"
            "  /edit [module]    open a module (main by default) in the Editor tab";

        String TypeOf(lua::TypeNameFn type) { return type ? type() : String("any"); }

        /// "(a: number, b: Vector3) -> Entity"
        String Signature(const lua::ApiFunction& function)
        {
            String text = "(";
            for (size_t index = 0; index < function.params.size(); ++index) {
                if (index)
                    text += ", ";
                text += function.params[index].name + ": " + TypeOf(function.params[index].type);
            }
            if (function.variadic)
                text += function.params.empty() ? "..." : ", ...";
            text += ")";
            for (size_t index = 0; index < function.results.size(); ++index)
                text += (index ? ", " : " -> ") + TypeOf(function.results[index]);
            return text;
        }

        /// What a click inserts into the console: the call with its parameter names.
        String CallText(StringView owner, const char* separator, const lua::ApiFunction& function)
        {
            String text = String(owner) + separator + (function.kind == lua::ApiFunction::Kind::Constructor ? String() : function.name) + "(";
            for (size_t index = 0; index < function.params.size(); ++index)
                text += (index ? ", " : "") + function.params[index].name;
            return text + ")";
        }

        bool ContainsNoCase(StringView text, StringView part)
        {
            if (part.empty())
                return true;
            const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
            return std::search(text.begin(), text.end(), part.begin(), part.end(),
                [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

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

    } // namespace

    ScriptWindow::ScriptWindow(Settings& settings, ScriptSystem& scripts)
        : IImGuiWindow(settings)
        , m_scripts(scripts)
        , m_editor(scripts)
    {
        Print(LineKind::Info, "Lua console. /help lists the commands, Tab completes names.");
    }

    void ScriptWindow::Render(ImGuiWindowContext&)
    {
        if (!m_visible)
            return;

        ImGui::SetNextWindowSize(ImVec2(760.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(GetName().data(), &m_visible)) {
            ImGui::End();
            return;
        }

        DrawToolbar();
        if (ImGui::BeginTabBar("ScriptTabs")) {
            if (ImGui::BeginTabItem("Console", nullptr, m_selectConsole ? ImGuiTabItemFlags_SetSelected : 0)) {
                DrawConsole();
                ImGui::EndTabItem();
            }
            m_selectConsole = false;
            const String editorLabel = m_editor.UnsavedCount() ? "Editor *###Editor" : "Editor###Editor";
            if (ImGui::BeginTabItem(editorLabel.c_str(), nullptr, m_selectEditor ? ImGuiTabItemFlags_SetSelected : 0)) {
                m_editor.Draw();
                ImGui::EndTabItem();
            }
            m_selectEditor = false;
            if (ImGui::BeginTabItem("Modules")) {
                DrawModules();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("API")) {
                DrawApi();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void ScriptWindow::DrawToolbar()
    {
        if (!m_scripts.IsInitialized()) {
            ImGui::TextColored(ErrorColor, "Scripts are not initialized");
            return;
        }

        ImGui::TextDisabled("%s", LUA_RELEASE);
        ImGui::SameLine();
        const String& mainName = m_scripts.MainModuleName();
        switch (m_scripts.GetMainState()) {
        case ScriptSystem::MainState::Running:
            ImGui::TextColored(OkColor, "main: %s (running)", mainName.c_str());
            break;
        case ScriptSystem::MainState::Faulted:
            ImGui::TextColored(ErrorColor, "main: %s (stopped)", mainName.c_str());
            break;
        case ScriptSystem::MainState::Idle:
            ImGui::TextColored(InfoColor, "main: %s (not running)", mainName.empty() ? "-" : mainName.c_str());
            break;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %.1f KB", static_cast<double>(m_scripts.GetState().MemoryUsage()) / 1024.0);

        bool hotReload = m_settings.Get<bool>(Settings::Category::Script, ScriptSystem::HotReloadSetting, true);
        if (ImGui::Checkbox("Hot reload", &hotReload))
            m_settings.Set(Settings::Category::Script, ScriptSystem::HotReloadSetting, hotReload);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reload a module as soon as its file is saved");
        ImGui::SameLine();
        if (ImGui::Button("Reload changed")) {
            const size_t reloaded = m_scripts.ReloadChanged();
            Print(LineKind::Info, "Reloaded " + String(std::to_string(reloaded)) + " changed module(s)");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_scripts.GetMainState() == ScriptSystem::MainState::Idle);
        if (ImGui::Button("Rebuild scene"))
            RebuildScene();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Calls main.build_scene()");
        ImGui::SameLine();
        if (ImGui::Button("Edit main"))
            Edit(m_scripts.MainModuleName());
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            m_scripts.RequestRestart();
            Print(LineKind::Info, "Scripts restart on the next frame");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("New Lua state: all modules and their state are loaded again");

        if (m_scripts.GetMainState() == ScriptSystem::MainState::Faulted) {
            ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
            ImGui::TextWrapped("%s", m_scripts.MainError().c_str());
            ImGui::PopStyleColor();
            if (ImGui::SmallButton("Reload main module")) {
                if (auto reloaded = m_scripts.ReloadModule(m_scripts.MainModuleName()); !reloaded)
                    Print(LineKind::Error, reloaded.error().message);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Show the error line"))
                EditLocation(m_scripts.MainError());
        }
        ImGui::Separator();
    }

    void ScriptWindow::DrawConsole()
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
                const ImVec4 color = line.kind == LineKind::Command ? CommandColor
                    : line.kind == LineKind::Error                  ? ErrorColor
                    : line.kind == LineKind::Info                   ? InfoColor
                                                                    : OutputColor;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
                ImGui::PopStyleColor();
            }
            if (m_scrollToBottom || atBottom)
                ImGui::SetScrollHereY(1.0f);
            m_scrollToBottom = false;
        }
        ImGui::EndChild();

        if (m_focusInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory
            | ImGuiInputTextFlags_CallbackCompletion;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Run").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
        const bool entered = ImGui::InputTextWithHint("##input", "Lua, e.g. elm.scene.count()", m_input.data(), m_input.size(), flags,
            &ScriptWindow::InputCallback, this);
        ImGui::SameLine();
        const bool run = ImGui::Button("Run");
        if (entered || run) {
            Submit();
            m_focusInput = true;
        }
        ImGui::TextDisabled("Enter run | Tab complete | Up/Down history | /help");
    }

    void ScriptWindow::DrawModules()
    {
        const auto& modules = m_scripts.LoadedModules();
        if (modules.empty()) {
            ImGui::TextDisabled("No modules loaded");
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("Modules", 4, flags))
            return;
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Reloads", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);

        String edit;
        String editError;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Reloading changes the list: remember the click and act after the loop
        String reload;
        for (const auto& module : modules) {
            ImGui::PushID(module.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(module.name.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", module.path.string().c_str());
            if (module.name == m_scripts.MainModuleName()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(main)");
            }

            ImGui::TableNextColumn();
            if (!module.error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
                ImGui::TextWrapped("%s", module.error.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to open the error line");
                if (ImGui::IsItemClicked())
                    editError = module.error;
            } else if (module.pendingWriteTime != 0) {
                ImGui::TextColored(WarningColor, "changed, reloading");
            } else {
                ImGui::TextColored(OkColor, "ok");
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u", module.reloadCount);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Reload"))
                reload = module.name;
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
                edit = module.name;
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (!edit.empty())
            Edit(edit);
        if (!editError.empty())
            EditLocation(editError);

        if (!reload.empty()) {
            if (auto reloaded = m_scripts.ReloadModule(reload))
                Print(LineKind::Info, "Reloaded " + reload);
            else
                Print(LineKind::Error, reloaded.error().message);
        }
    }

    void ScriptWindow::DrawApi()
    {
        if (!m_scripts.IsInitialized())
            return;
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter, e.g. spawn or Vector3", m_apiFilter.data(), m_apiFilter.size());
        const StringView filter(m_apiFilter.data());

        if (!ImGui::BeginChild("ApiList", ImVec2(0.0f, 0.0f), false))
            return ImGui::EndChild();

        const auto& api = m_scripts.GetState().Api();
        auto entry = [&](const String& label, const String& detail, const String& insert) {
            if (!ContainsNoCase(label, filter) && !ContainsNoCase(detail, filter))
                return;
            if (ImGui::Selectable(label.c_str()))
                InsertIntoInput(insert);
            if (!detail.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", detail.c_str());
            }
        };

        for (const auto& module : api.Modules()) {
            if (!ImGui::CollapsingHeader(module.path.c_str(), filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            ImGui::PushID(module.path.c_str());
            for (const auto& function : module.functions)
                entry(module.path + "." + function.name, Signature(function), CallText(module.path, ".", function));
            for (const auto& value : module.values)
                entry(module.path + "." + value.name, ": " + TypeOf(value.type), module.path + "." + value.name);
            ImGui::PopID();
        }

        for (const auto& added : api.Classes()) {
            const String header = added.name + "  (" + added.module + "." + added.name + ")";
            if (!ImGui::CollapsingHeader(header.c_str(), filter.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            ImGui::PushID(header.c_str());
            const String qualified = added.module + "." + added.name;
            for (const auto& function : added.functions) {
                const bool method = function.kind == lua::ApiFunction::Kind::Method;
                const bool constructor = function.kind == lua::ApiFunction::Kind::Constructor;
                const String label = constructor ? added.name + "(...)" : (method ? ":" : ".") + function.name;
                entry(label, Signature(function), constructor ? CallText(qualified, "", function)
                    : method ? CallText("", ":", function) : CallText(qualified, ".", function));
            }
            for (const auto& field : added.fields)
                entry("." + field.name, ": " + TypeOf(field.type) + (field.readOnly ? " (read only)" : ""), "." + field.name);
            if (!added.metamethods.empty()) {
                String operators;
                for (const auto& name : added.metamethods)
                    operators += (operators.empty() ? "" : " ") + name;
                ImGui::TextDisabled("metamethods: %s", operators.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void ScriptWindow::Submit()
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

        Print(LineKind::Command, "> " + line);
        m_scrollToBottom = true;
        if (line.starts_with("/")) {
            RunCommand(StringView(line).substr(1));
            return;
        }
        if (auto result = m_scripts.Execute(line)) {
            if (!result->empty())
                Print(LineKind::Output, *result);
        } else {
            Print(LineKind::Error, result.error().message);
        }
    }

    void ScriptWindow::RunCommand(StringView command)
    {
        const size_t space = command.find(' ');
        const StringView name = command.substr(0, space);
        const StringView argument = space == StringView::npos ? StringView() : Trim(command.substr(space + 1));

        if (name == "help") {
            Print(LineKind::Info, HelpText);
        } else if (name == "clear") {
            m_lines.clear();
        } else if (name == "modules") {
            for (const auto& module : m_scripts.LoadedModules())
                Print(module.error.empty() ? LineKind::Output : LineKind::Error,
                    module.name + (module.error.empty() ? "" : "  " + module.error));
        } else if (name == "reload") {
            if (argument.empty()) {
                Print(LineKind::Info, "Reloaded " + String(std::to_string(m_scripts.ReloadChanged())) + " changed module(s)");
            } else if (auto reloaded = m_scripts.ReloadModule(argument)) {
                Print(LineKind::Info, "Reloaded " + String(argument));
            } else {
                Print(LineKind::Error, reloaded.error().message);
            }
        } else if (name == "scene") {
            RebuildScene();
        } else if (name == "edit") {
            Edit(argument.empty() ? StringView(m_scripts.MainModuleName()) : argument);
        } else if (name == "restart") {
            m_scripts.RequestRestart();
            Print(LineKind::Info, "Scripts restart on the next frame");
        } else {
            Print(LineKind::Error, "Unknown command /" + String(name) + ", see /help");
        }
    }

    void ScriptWindow::Print(LineKind kind, StringView text)
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

    void ScriptWindow::RebuildScene()
    {
        if (auto built = m_scripts.CallMain(ScriptSystem::BuildSceneFunction); !built)
            Print(LineKind::Error, built.error().message);
        else if (!*built)
            Print(LineKind::Error, "The main module has no build_scene() function");
        else
            Print(LineKind::Info, "Scene rebuilt by " + m_scripts.MainModuleName() + ".build_scene()");
        m_scrollToBottom = true;
    }

    void ScriptWindow::Edit(StringView module, int line)
    {
        if (m_editor.OpenModule(module, line))
            m_selectEditor = true;
        else
            Print(LineKind::Error, "No file for module '" + String(module) + "'");
    }

    void ScriptWindow::EditLocation(StringView message)
    {
        if (m_editor.OpenLocation(message))
            m_selectEditor = true;
        else
            Print(LineKind::Error, "The message names no script line");
    }

    void ScriptWindow::InsertIntoInput(StringView text)
    {
        const size_t length = std::strlen(m_input.data());
        const size_t count = (std::min)(text.size(), m_input.size() - 1 - length);
        std::memcpy(m_input.data() + length, text.data(), count);
        m_input[length + count] = '\0';
        m_selectConsole = true;
        m_focusInput = true;
    }

    int ScriptWindow::InputCallback(ImGuiInputTextCallbackData* data)
    {
        auto& window = *static_cast<ScriptWindow*>(data->UserData);
        auto replace = [data](StringView text) {
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, text.data(), text.data() + text.size());
        };

        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            auto& history = window.m_history;
            if (history.empty())
                return 0;
            int& position = window.m_historyPosition;
            if (data->EventKey == ImGuiKey_UpArrow)
                position = position < 0 ? static_cast<int>(history.size()) - 1 : (std::max)(position - 1, 0);
            else if (data->EventKey == ImGuiKey_DownArrow && position >= 0)
                position = position + 1 < static_cast<int>(history.size()) ? position + 1 : -1;
            replace(position < 0 ? StringView() : StringView(history[static_cast<size_t>(position)]));
        } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            // Complete the text up to the cursor; the rest of the line stays after it
            const StringView before(data->Buf, static_cast<size_t>(data->CursorPos));
            const String after(data->Buf + data->CursorPos);
            const auto completions = window.m_scripts.Complete(before);
            if (completions.empty())
                return 0;
            const String completed = completions.size() == 1 ? completions.front() : CommonPrefix(completions);
            if (completions.size() > 1) {
                String list;
                for (const auto& completion : completions) {
                    const size_t separator = completion.find_last_of(".: (=,");
                    list += (list.empty() ? "" : "  ") + completion.substr(separator == String::npos ? 0 : separator + 1);
                }
                window.Print(LineKind::Info, list);
                window.m_scrollToBottom = true;
            }
            replace(completed + after);
            data->CursorPos = static_cast<int>(completed.size());
        }
        return 0;
    }

} // namespace elm::script
