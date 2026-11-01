#pragma once

#include "core/Std.hpp"

namespace elm::script {

    /// One indent level of the script editor: the scripts are indented with four spaces.
    inline constexpr StringView IndentUnit { "    " };

    /// What Tab / Ctrl+] (Indent) and Ctrl+[ (Outdent) ask the editor to do with the selection.
    enum class IndentEdit : uint8_t {
        None,
        Indent,
        Outdent,
    };

    /// Text after an indentation edit and the byte range to select in it (end is the cursor).
    struct IndentedText {
        String text;
        size_t selectionBegin { 0 };
        size_t selectionEnd { 0 };
    };

    /// Leading spaces and tabs of the line that holds offset.
    [[nodiscard]] StringView LineIndent(StringView text, size_t offset);

    /// Indent: a selection inside one line is replaced by IndentUnit; a selection over several
    /// lines adds IndentUnit to each non-empty line of it. Outdent removes up to one IndentUnit
    /// (or one tab) from each selected line. A selection that ends right after a newline leaves the
    /// next line out. [begin, end) are byte offsets, begin <= end.
    [[nodiscard]] IndentedText ApplyIndentEdit(StringView text, size_t begin, size_t end, IndentEdit edit);

} // namespace elm::script
