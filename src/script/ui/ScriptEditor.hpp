#pragma once

#include "script/ScriptSystem.hpp"
#include "script/ui/LuaHighlighter.hpp"

#include <array>
#include <chrono>
#include <optional>

struct ImGuiInputTextCallbackData;
struct ImVec2;
typedef unsigned int ImGuiID;

namespace elm::script {

/// Text editor for the files under the script root, drawn inside the Scripts window.
/// Saving (Ctrl+S) writes the file and reloads its module right away, a failed reload shows
/// the error with its line; the running version stays until the file loads.
///
/// Lua syntax coloring: ImGui's multiline text field has a single text color, so the field draws
/// its text transparent and DrawHighlight paints the colored text, the cursor, the error line and
/// the line numbers over it at the same positions (it reads the field's child window and scroll,
/// imgui_internal.h: check it when ImGui is updated).
class ScriptEditor {
public:
    /// "demo/spinner.lua:12: ..." -> { "demo/spinner.lua", 12 }: the first file:line in a Lua message.
    struct Location {
        String file;
        int line { 0 };
    };
    [[nodiscard]] static std::optional<Location> FindLocation(StringView message);

    explicit ScriptEditor(ScriptSystem& scripts);

    ScriptEditor(const ScriptEditor&) = delete;
    ScriptEditor& operator=(const ScriptEditor&) = delete;

    /// Opens a file in a tab (or brings its tab forward); line > 0 moves the cursor there.
    void Open(const Path& path, int line = 0);
    /// Opens the file of a module by name, false when there is no such file.
    bool OpenModule(StringView module, int line = 0);
    /// Opens the file named in an error message at its line, false when it names none.
    bool OpenLocation(StringView message);

    void Draw();
    [[nodiscard]] size_t UnsavedCount() const;

private:
    struct Document {
        Path path;
        /// Path relative to the script root, as Lua names the file in errors.
        String relative;
        String text;
        String saved;
        int64_t diskTime { 0 };
        bool changedOnDisk { false };
        bool select { false };
        /// Pending cursor move, applied while the text field is active.
        int gotoLine { 0 };
        /// The text was replaced from outside the text field (revert, a change on disk).
        bool textReplaced { false };
        int cursorLine { 1 };
        int cursorColumn { 1 };
        /// Byte offset of the cursor, set while the text field is active.
        size_t cursorByte { 0 };
        /// Syntax spans of tokenizedText, redone when the text changes.
        Vector<LuaSpan> spans;
        String tokenizedText;
        String status;
        bool statusError { false };
        int errorLine { 0 };

        [[nodiscard]] bool Dirty() const { return text != saved; }
    };

    void DrawFiles();
    void DrawDocument(Document& document);
    void DrawClosePrompt();
    void DrawHighlight(Document& document, ImGuiID textId, const ImVec2& gutterMin, float gutterWidth);
    void Save(Document& document);
    void Revert(Document& document);
    void CheckDisk(Document& document);
    void RefreshFiles();
    void CreateFile();
    void Close(Document* document);
    [[nodiscard]] Document* Find(const Path& path);
    /// Error recorded on a loaded module (a failed load or on_reload), empty when it is fine.
    [[nodiscard]] String ModuleError(StringView module) const;
    static int TextCallback(ImGuiInputTextCallbackData* data);

    ScriptSystem& m_scripts;
    /// Pointers stay valid while a tab is open: the text callback holds one.
    Vector<UniquePtr<Document>> m_documents;
    Document* m_active { nullptr };
    Document* m_closing { nullptr };
    Vector<Path> m_files;
    std::chrono::steady_clock::time_point m_nextRefresh { };
    std::chrono::steady_clock::time_point m_nextDiskCheck { };
    std::array<char, 128> m_newFile { };
    std::array<char, 128> m_fileFilter { };
    float m_filesWidth { 250.0f };
    bool m_showFiles { true };
};

} // namespace elm::script
