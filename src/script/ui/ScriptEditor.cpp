#include "script/ui/ScriptEditor.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cstring>
#include <iterator>

namespace elm::script {

namespace {

    constexpr auto RefreshInterval = std::chrono::seconds(2);
    constexpr auto DiskCheckInterval = std::chrono::milliseconds(500);

    const ImVec4 ErrorColor { 1.0f, 0.40f, 0.36f, 1.0f };
    const ImVec4 OkColor { 0.40f, 0.85f, 0.50f, 1.0f };
    const ImVec4 WarningColor { 1.0f, 0.78f, 0.35f, 1.0f };

    // Syntax colors, close to the usual dark editor themes
    ImU32 TokenColor(LuaToken kind)
    {
        switch (kind) {
        case LuaToken::Keyword:
            return IM_COL32(86, 156, 214, 255);
        case LuaToken::Constant:
            return IM_COL32(86, 156, 214, 255);
        case LuaToken::String:
            return IM_COL32(206, 145, 120, 255);
        case LuaToken::Number:
            return IM_COL32(181, 206, 168, 255);
        case LuaToken::Comment:
            return IM_COL32(106, 153, 85, 255);
        case LuaToken::Builtin:
            return IM_COL32(78, 201, 176, 255);
        case LuaToken::FunctionName:
            return IM_COL32(220, 220, 170, 255);
        case LuaToken::Text:
            break;
        }
        return IM_COL32(212, 212, 212, 255);
    }

    constexpr ImU32 CursorColor = IM_COL32(230, 230, 230, 255);
    constexpr ImU32 CurrentLineColor = IM_COL32(255, 255, 255, 12);
    constexpr ImU32 ErrorLineColor = IM_COL32(255, 70, 60, 60);
    constexpr ImU32 LineNumberColor = IM_COL32(110, 118, 129, 255);
    constexpr ImU32 CurrentLineNumberColor = IM_COL32(200, 200, 200, 255);
    constexpr ImU32 ErrorLineNumberColor = IM_COL32(255, 100, 90, 255);

    constexpr const char* NewModuleTemplate = "local M = {}\n"
                                              "\n"
                                              "return M\n";

    auto ReadFile(const Path& path) -> std::optional<String>
    {
        auto stream = FileSystem::OpenForReading(path, std::ios::in | std::ios::binary);
        if (!stream)
            return std::nullopt;
        String text { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        if (text.starts_with("\xEF\xBB\xBF"))
            text.erase(0, 3);
        return text;
    }

    bool WriteFile(const Path& path, StringView text)
    {
        std::error_code error;
        FileSystem::CreateDirectories(path.Parent(), error);
        auto stream = FileSystem::OpenForWriting(path, std::ios::out | std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(stream);
    }

    int64_t WriteTimeOf(const Path& path)
    {
        std::error_code error;
        return FileSystem::LastWriteTime(path, error);
    }

    /// Every .lua file under directory, folders starting with '.' (.meta) skipped.
    void CollectScripts(const Path& directory, Vector<Path>& files)
    {
        std::error_code error;
        for (auto it = Directory(directory).Begin(error); !error && !it.IsEnd(); it.Next(error)) {
            const Path path = it.CurrentPath();
            const String name(path.Filename().string().c_str());
            std::error_code typeError;
            if (it.IsDirectory(typeError)) {
                if (!name.starts_with("."))
                    CollectScripts(path, files);
            } else if (name.ends_with(".lua")) {
                files.push_back(path);
            }
        }
    }

    /// Byte offset where a 1-based line starts.
    int OffsetOfLine(StringView text, int line)
    {
        size_t offset = 0;
        for (int current = 1; current < line; ++current) {
            const size_t next = text.find('\n', offset);
            if (next == StringView::npos)
                break;
            offset = next + 1;
        }
        return static_cast<int>(offset);
    }

} // namespace

auto ScriptEditor::FindLocation(StringView message) -> std::optional<Location>
{
    for (size_t found = message.find(".lua:"); found != StringView::npos; found = message.find(".lua:", found + 1)) {
        // The file name runs back to a space, a quote or the start of a line
        size_t begin = found;
        while (begin > 0 && message[begin - 1] != ' ' && message[begin - 1] != '\t' && message[begin - 1] != '\n'
            && message[begin - 1] != '\'' && message[begin - 1] != '"' && message[begin - 1] != '@')
            --begin;
        const size_t digits = found + 5;
        int line = 0;
        const auto [end, error] = std::from_chars(message.data() + digits, message.data() + message.size(), line);
        if (error == std::errc() && end != message.data() + digits && line > 0)
            return Location { String(message.substr(begin, found + 4 - begin)), line };
    }
    return std::nullopt;
}

ScriptEditor::ScriptEditor(ScriptSystem& scripts)
    : m_scripts(scripts)
{
}

void ScriptEditor::Open(const Path& path, int line)
{
    Document* document = Find(path);
    if (!document) {
        auto text = ReadFile(path);
        if (!text)
            return;
        auto created = MakeUnique<Document>();
        created->path = path;
        created->relative = String(path.LexicallyRelative(m_scripts.ScriptRoot()).generic_string().c_str());
        created->text = *text;
        created->saved = std::move(*text);
        created->diskTime = WriteTimeOf(path);
        // A module that already failed shows its error line right away
        if (const String error = ModuleError(m_scripts.ModuleNameOf(path)); !error.empty()) {
            created->status = error;
            created->statusError = true;
            if (const auto location = FindLocation(error); location && location->file == created->relative)
                created->errorLine = location->line;
        }
        document = created.get();
        m_documents.push_back(std::move(created));
    }
    document->select = true;
    if (line > 0)
        document->gotoLine = line;
}

bool ScriptEditor::OpenModule(StringView module, int line)
{
    const Path path = m_scripts.FindModule(module);
    if (path.Empty())
        return false;
    Open(path, line);
    return true;
}

bool ScriptEditor::OpenLocation(StringView message)
{
    const auto location = FindLocation(message);
    if (!location)
        return false;
    const Path path = m_scripts.ScriptRoot() / location->file;
    if (!FileSystem::Exists(path))
        return false;
    Open(path, location->line);
    return true;
}

size_t ScriptEditor::UnsavedCount() const
{
    return static_cast<size_t>(std::count_if(m_documents.begin(), m_documents.end(),
        [](const auto& document) { return document->Dirty(); }));
}

void ScriptEditor::Draw()
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_nextRefresh) {
        RefreshFiles();
        m_nextRefresh = now + RefreshInterval;
    }
    if (now >= m_nextDiskCheck) {
        for (auto& document : m_documents)
            CheckDisk(*document);
        m_nextDiskCheck = now + DiskCheckInterval;
    }

    // Keep the file browser optional: code editing should be able to use the
    // full window width. The toggle remains available when the browser is hidden.
    if (ImGui::SmallButton(m_showFiles ? "Hide files  <" : ">  Show files"))
        m_showFiles = !m_showFiles;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu open  |  %zu unsaved", m_documents.size(), UnsavedCount());
    ImGui::Separator();

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (m_showFiles) {
        m_filesWidth = (std::clamp)(m_filesWidth, 180.0f, (std::max)(180.0f, availableWidth * 0.45f));
        if (ImGui::BeginChild("Files", ImVec2(m_filesWidth, 0.0f), true))
            DrawFiles();
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::InvisibleButton("##ScriptEditorSplitter", ImVec2(5.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive()) {
            m_filesWidth += ImGui::GetIO().MouseDelta.x;
            m_filesWidth = (std::clamp)(m_filesWidth, 180.0f, (std::max)(180.0f, availableWidth * 0.45f));
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine();
    }

    if (ImGui::BeginChild("Documents", ImVec2(0.0f, 0.0f), false)) {
        if (m_documents.empty()) {
            ImGui::TextDisabled("Open a script from the list on the left.");
            ImGui::TextDisabled("Ctrl+S saves the file and reloads its module in the running engine.");
        } else if (ImGui::BeginTabBar("DocumentTabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll)) {
            Document* close = nullptr;
            for (auto& owned : m_documents) {
                Document& document = *owned;
                ImGuiTabItemFlags flags = document.Dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
                if (document.select) {
                    flags |= ImGuiTabItemFlags_SetSelected;
                    document.select = false;
                }
                bool open = true;
                const String label = document.relative + "###" + document.relative;
                if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                    m_active = &document;
                    DrawDocument(document);
                    ImGui::EndTabItem();
                }
                if (!open)
                    close = &document;
            }
            ImGui::EndTabBar();
            if (close) {
                if (close->Dirty()) {
                    m_closing = close;
                    ImGui::OpenPopup("Unsaved changes");
                } else {
                    Close(close);
                }
            }
        }
        DrawClosePrompt();
    }
    ImGui::EndChild();

    const auto& io = ImGui::GetIO();
    if (m_active && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)
        && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        Save(*m_active);
}

void ScriptEditor::DrawFiles()
{
    const String mainModule = m_scripts.MainModuleName();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scripts");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", m_files.size());

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##script-filter", "Filter scripts...", m_fileFilter.data(), m_fileFilter.size());
    if (m_fileFilter[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            m_fileFilter.fill('\0');
    }

    ImGui::Separator();

    size_t visibleFiles = 0;
    const StringView filter(m_fileFilter.data());
    for (const auto& file : m_files) {
        const String relative(file.LexicallyRelative(m_scripts.ScriptRoot()).generic_string().c_str());
        if (!filter.empty() && relative.find(filter) == String::npos)
            continue;

        ++visibleFiles;
        const bool isMain = !mainModule.empty() && m_scripts.ModuleNameOf(file) == mainModule;
        const Document* open = Find(file);
        String label = relative;
        if (open && open->Dirty())
            label += " *";
        if (isMain)
            label += "  [main]";

        ImGui::PushID(relative.c_str());
        if (ImGui::Selectable(label.c_str(), open && open == m_active))
            Open(file);
        ImGui::PopID();
    }

    if (visibleFiles == 0)
        ImGui::TextDisabled(filter.empty() ? "No Lua scripts found." : "No matching scripts.");

    ImGui::Separator();
    ImGui::TextDisabled("Create");
    ImGui::SetNextItemWidth(-1.0f);
    const bool create = ImGui::InputTextWithHint("##new", "folder/name.lua", m_newFile.data(), m_newFile.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("New file", ImVec2(-1.0f, 0.0f)) || create)
        CreateFile();

    if (ImGui::Button("Refresh", ImVec2(-1.0f, 0.0f)))
        RefreshFiles();
}

void ScriptEditor::DrawDocument(Document& document)
{
    const String module = m_scripts.ModuleNameOf(document.path);
    const auto& loaded = m_scripts.LoadedModules();
    const auto running = std::find_if(loaded.begin(), loaded.end(), [&](const auto& entry) { return entry.name == module; });
    const bool isLoaded = running != loaded.end();

    if (ImGui::Button("Save  Ctrl+S"))
        Save(document);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+S: save and reload the module");
    ImGui::SameLine();
    ImGui::BeginDisabled(!document.Dirty());
    if (ImGui::Button("Revert"))
        Revert(document);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!isLoaded);
    if (ImGui::Button("Reload module")) {
        document.errorLine = 0;
        if (auto reloaded = m_scripts.ReloadModule(module); !reloaded) {
            document.status = reloaded.error().message;
            document.statusError = true;
        } else if (const String hookError = ModuleError(module); !hookError.empty()) {
            document.status = "Reloaded, but on_reload failed:\n" + hookError;
            document.statusError = true;
        } else {
            document.status = "Reloaded " + module;
            document.statusError = false;
        }
        if (const auto location = FindLocation(document.status); document.statusError && location && location->file == document.relative)
            document.errorLine = location->line;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", module.empty() ? "no module" : module.c_str());
    ImGui::SameLine();
    if (isLoaded && running->error.empty())
        ImGui::TextColored(OkColor, "● Running");
    else if (isLoaded)
        ImGui::TextColored(ErrorColor, "● Error");
    else
        ImGui::TextDisabled("● Not loaded");

    if (document.changedOnDisk) {
        ImGui::TextColored(WarningColor, "The file changed on disk.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Load the disk version")) {
            Revert(document);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Keep mine"))
            document.changedOnDisk = false;
    }
    if (!document.status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, document.statusError ? ErrorColor : OkColor);
        ImGui::TextWrapped("%s", document.status.c_str());
        ImGui::PopStyleColor();
        if (document.statusError && document.errorLine > 0) {
            const String label = "Go to line " + String(std::to_string(document.errorLine));
            if (ImGui::SmallButton(label.c_str()))
                document.gotoLine = document.errorLine;
        }
    }

    // An active text field keeps its own copy of the text and would write it back over a
    // replaced one: let it go for a frame so it reads the new text, then take the focus again
    const ImGuiID textId = ImGui::GetID("##text");
    bool refocus = false;
    if (document.textReplaced) {
        document.textReplaced = false;
        if (ImGui::GetActiveID() == textId) {
            ImGui::ClearActiveID();
            refocus = true;
        }
    }
    // The text field takes focus to receive a cursor move
    if (document.gotoLine > 0 || refocus)
        ImGui::SetKeyboardFocusHere();
    // Line numbers on the left, then the text field: it draws its text transparent and
    // DrawHighlight paints the colored text over it
    const float statusHeight = ImGui::GetTextLineHeightWithSpacing();
    const float height = (std::max)(ImGui::GetContentRegionAvail().y - statusHeight, ImGui::GetFontSize() * 3.0f);
    const auto lineCount = 1 + std::count(document.text.begin(), document.text.end(), '\n');
    const String widest(std::to_string(lineCount).size() + 1, '0');
    const float gutterWidth = ImGui::CalcTextSize(widest.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 gutterMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(gutterWidth, height));
    ImGui::SameLine(0.0f, 0.0f);

    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize
        | ImGuiInputTextFlags_CallbackAlways;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::InputTextMultiline("##text", document.text.data(), document.text.capacity() + 1,
        ImVec2(-1.0f, height), flags, &ScriptEditor::TextCallback, &document);
    ImGui::PopStyleColor();
    DrawHighlight(document, textId, gutterMin, gutterWidth);

    ImGui::Separator();
    ImGui::TextDisabled("Ln %d  Col %d", document.cursorLine, document.cursorColumn);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (document.Dirty())
        ImGui::TextColored(WarningColor, "Modified");
    else
        ImGui::TextColored(OkColor, "Saved");
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", document.relative.c_str());
}

void ScriptEditor::DrawHighlight(Document& document, ImGuiID textId, const ImVec2& gutterMin, float gutterWidth)
{
    // The child window InputTextMultiline draws into
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    ImGuiWindow* field = nullptr;
    for (ImGuiWindow* child : parent->DC.ChildWindows) {
        if (child->ChildId == textId)
            field = child;
    }
    if (!field)
        return;

    if (document.tokenizedText != document.text) {
        TokenizeLua(document.text, document.spans);
        document.tokenizedText = document.text;
    }

    // Same origin as the field's own text: frame padding inside the child, its vertical scroll,
    // and the horizontal scroll it keeps while active
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const ImGuiInputTextState* state = ImGui::GetActiveID() == textId ? ImGui::GetInputTextState(textId) : nullptr;
    const float scrollX = state ? state->ScrollX : 0.0f;
    const ImVec2 origin(field->Pos.x + padding.x - scrollX, field->Pos.y + padding.y - field->Scroll.y);
    const ImRect clip = field->InnerClipRect;
    const int firstLine = (std::max)(0, static_cast<int>((clip.Min.y - origin.y) / fontSize));
    const int lastLine = static_cast<int>((clip.Max.y - origin.y) / fontSize) + 1;

    ImDrawList* draw = field->DrawList;
    draw->PushClipRect(clip.Min, clip.Max, true);

    auto lineBackground = [&](int line, ImU32 color) {
        const float y = origin.y + static_cast<float>(line - 1) * fontSize;
        draw->AddRectFilled(ImVec2(clip.Min.x, y), ImVec2(clip.Max.x, y + fontSize), color);
    };
    if (state)
        lineBackground(document.cursorLine, CurrentLineColor);
    if (document.errorLine > 0)
        lineBackground(document.errorLine, ErrorLineColor);

    // Colored text, visible lines only
    const char* text = document.text.data();
    int line = 0;
    float x = origin.x;
    for (const auto& span : document.spans) {
        if (line > lastLine)
            break;
        const ImU32 color = TokenColor(span.kind);
        size_t at = span.begin;
        while (at < span.end && line <= lastLine) {
            const char* found = static_cast<const char*>(std::memchr(text + at, '\n', span.end - at));
            const size_t segmentEnd = found ? static_cast<size_t>(found - text) : span.end;
            if (line >= firstLine && segmentEnd > at) {
                const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text + at, text + segmentEnd).x;
                if (x < clip.Max.x && x + width > clip.Min.x)
                    draw->AddText(font, fontSize, ImVec2(x, origin.y + static_cast<float>(line) * fontSize), color,
                        text + at, text + segmentEnd);
                x += width;
            }
            if (found) {
                ++line;
                x = origin.x;
                at = segmentEnd + 1;
            } else {
                at = segmentEnd;
            }
        }
    }

    // The field's cursor has the (transparent) text color: draw it the way ImGui does
    if (state) {
        const bool blinkOn = !ImGui::GetIO().ConfigInputTextCursorBlink || state->CursorAnim <= 0.0f
            || ImFmod(state->CursorAnim, 1.20f) <= 0.80f;
        if (blinkOn) {
            const size_t cursor = (std::min)(document.cursorByte, document.text.size());
            const size_t lineStart = StringView(document.text).substr(0, cursor).rfind('\n');
            const size_t from = lineStart == StringView::npos ? 0 : lineStart + 1;
            const float cursorX = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text + from, text + cursor).x;
            const ImVec2 position = ImFloor(ImVec2(origin.x + cursorX, origin.y + static_cast<float>(document.cursorLine) * fontSize));
            draw->AddLine(ImVec2(position.x, position.y - fontSize + 0.5f), ImVec2(position.x, position.y - 1.5f), CursorColor);
        }
    }
    draw->PopClipRect();

    // Line numbers, right aligned in the gutter, scrolled with the text
    ImDrawList* gutter = parent->DrawList;
    gutter->PushClipRect(ImVec2(gutterMin.x, clip.Min.y), ImVec2(gutterMin.x + gutterWidth, clip.Max.y), true);
    const int totalLines = 1 + static_cast<int>(std::count(document.text.begin(), document.text.end(), '\n'));
    const float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
    for (int index = firstLine; index <= (std::min)(lastLine, totalLines - 1); ++index) {
        const auto number = std::to_string(index + 1);
        const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, number.c_str()).x;
        const ImU32 color = index + 1 == document.errorLine ? ErrorLineNumberColor
            : (state && index + 1 == document.cursorLine)   ? CurrentLineNumberColor
                                                            : LineNumberColor;
        gutter->AddText(font, fontSize, ImVec2(gutterMin.x + gutterWidth - width - spacing, origin.y + static_cast<float>(index) * fontSize),
            color, number.c_str());
    }
    gutter->PopClipRect();
}

void ScriptEditor::DrawClosePrompt()
{
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (m_closing) {
        ImGui::Text("%s has unsaved changes.", m_closing->relative.c_str());
        if (ImGui::Button("Save")) {
            Save(*m_closing);
            Close(m_closing);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            Close(m_closing);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel") || !m_closing) {
        m_closing = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ScriptEditor::Save(Document& document)
{
    document.errorLine = 0;
    if (!WriteFile(document.path, document.text)) {
        document.status = "Cannot write " + String(document.path.string().c_str());
        document.statusError = true;
        return;
    }
    document.saved = document.text;
    document.diskTime = WriteTimeOf(document.path);
    document.changedOnDisk = false;

    const String module = m_scripts.ModuleNameOf(document.path);
    const auto& loaded = m_scripts.LoadedModules();
    const bool isLoaded = std::any_of(loaded.begin(), loaded.end(), [&](const auto& entry) { return entry.name == module; });
    if (!isLoaded) {
        document.status = module.empty() ? "Saved." : "Saved. Module " + module + " is not loaded: require it to run it.";
        document.statusError = false;
        return;
    }
    // Reloaded here, not by the file watcher: the result belongs next to the text
    if (auto reloaded = m_scripts.ReloadModule(module); !reloaded) {
        document.status = "Saved, but the reload failed (the previous version keeps running):\n" + reloaded.error().message;
        document.statusError = true;
    } else if (const String hookError = ModuleError(module); !hookError.empty()) {
        // The new code is installed, its on_reload failed
        document.status = "Saved and reloaded, but on_reload failed:\n" + hookError;
        document.statusError = true;
    } else {
        document.status = "Saved, " + module + " reloaded.";
        document.statusError = false;
    }
    if (const auto location = FindLocation(document.status); document.statusError && location && location->file == document.relative)
        document.errorLine = location->line;
}

void ScriptEditor::Revert(Document& document)
{
    if (auto text = ReadFile(document.path)) {
        document.text = *text;
        document.saved = std::move(*text);
        document.textReplaced = true;
    }
    document.diskTime = WriteTimeOf(document.path);
    document.changedOnDisk = false;
    document.status.clear();
}

void ScriptEditor::CheckDisk(Document& document)
{
    const int64_t diskTime = WriteTimeOf(document.path);
    if (diskTime == 0 || diskTime == document.diskTime)
        return;
    // Saved by another editor: follow it unless there are edits here
    if (document.Dirty()) {
        document.changedOnDisk = true;
        document.diskTime = diskTime;
    } else {
        Revert(document);
    }
}

void ScriptEditor::RefreshFiles()
{
    m_files.clear();
    CollectScripts(m_scripts.ScriptRoot(), m_files);
    std::sort(m_files.begin(), m_files.end(), [](const Path& a, const Path& b) { return a.generic_string() < b.generic_string(); });
}

void ScriptEditor::CreateFile()
{
    String name(m_newFile.data());
    m_newFile.fill('\0');
    while (!name.empty() && (name.back() == ' ' || name.back() == '/'))
        name.pop_back();
    if (name.empty())
        return;
    if (!name.ends_with(".lua"))
        name += ".lua";
    const Path path = m_scripts.ScriptRoot() / name;
    if (m_scripts.ModuleNameOf(path).empty())
        return; // outside the script root or not a valid module name
    if (!FileSystem::Exists(path) && !WriteFile(path, NewModuleTemplate))
        return;
    RefreshFiles();
    Open(path, 2);
}

void ScriptEditor::Close(Document* document)
{
    if (m_active == document)
        m_active = nullptr;
    if (m_closing == document)
        m_closing = nullptr;
    std::erase_if(m_documents, [document](const auto& owned) { return owned.get() == document; });
}

String ScriptEditor::ModuleError(StringView module) const
{
    for (const auto& loaded : m_scripts.LoadedModules()) {
        if (loaded.name == module)
            return loaded.error;
    }
    return { };
}

auto ScriptEditor::Find(const Path& path) -> Document*
{
    const auto key = path.generic_string();
    for (auto& document : m_documents) {
        if (document->path.generic_string() == key)
            return document.get();
    }
    return nullptr;
}

int ScriptEditor::TextCallback(ImGuiInputTextCallbackData* data)
{
    auto& document = *static_cast<Document*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        // The text field grows the String it edits
        document.text.resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = document.text.data();
        return 0;
    }

    const StringView text(data->Buf, static_cast<size_t>(data->BufTextLen));
    if (document.gotoLine > 0) {
        data->CursorPos = OffsetOfLine(text, document.gotoLine);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        document.gotoLine = 0;
    }
    document.cursorByte = static_cast<size_t>(data->CursorPos);
    const StringView before = text.substr(0, static_cast<size_t>(data->CursorPos));
    document.cursorLine = 1 + static_cast<int>(std::count(before.begin(), before.end(), '\n'));
    const size_t lineStart = before.rfind('\n');
    document.cursorColumn = 1 + static_cast<int>(lineStart == StringView::npos ? before.size() : before.size() - lineStart - 1);
    return 0;
}

} // namespace elm::script
