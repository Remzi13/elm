#include "script/ui/CodeIndent.hpp"

#include <algorithm>
#include <span>

namespace elm::script {

    namespace {

        /// Indentation inserted (Indent) or removed (Outdent) at the start of one line.
        struct LineChange {
            size_t at { 0 };
            size_t count { 0 };
        };

        size_t LineStart(StringView text, size_t offset)
        {
            const size_t newline = offset == 0 ? StringView::npos : text.rfind('\n', offset - 1);
            return newline == StringView::npos ? 0 : newline + 1;
        }

        Vector<size_t> SelectedLineStarts(StringView text, size_t begin, size_t end)
        {
            const size_t last = end > begin && text[end - 1] == '\n' ? end - 1 : end;
            Vector<size_t> starts;
            for (size_t start = LineStart(text, begin); start <= last;) {
                starts.push_back(start);
                const size_t newline = text.find('\n', start);
                if (newline == StringView::npos)
                    break;
                start = newline + 1;
            }
            return starts;
        }

        /// Length of the indentation Outdent removes from the line starting at start.
        size_t OutdentLength(StringView text, size_t start)
        {
            if (start < text.size() && text[start] == '\t')
                return 1;
            size_t count = 0;
            while (count < IndentUnit.size() && start + count < text.size() && text[start + count] == ' ')
                ++count;
            return count;
        }

        Vector<LineChange> CollectChanges(StringView text, size_t begin, size_t end, IndentEdit edit)
        {
            Vector<LineChange> changes;
            for (const size_t start : SelectedLineStarts(text, begin, end)) {
                const bool emptyLine = start >= text.size() || text[start] == '\n' || text[start] == '\r';
                const size_t count = edit == IndentEdit::Indent ? (emptyLine ? 0 : IndentUnit.size()) : OutdentLength(text, start);
                if (count > 0)
                    changes.push_back({ start, count });
            }
            return changes;
        }

        /// Where an offset of the old text ends up: past insertions before it, back over removals.
        size_t MapOffset(size_t offset, std::span<const LineChange> changes, IndentEdit edit)
        {
            size_t mapped = offset;
            for (const auto& change : changes) {
                if (change.at >= offset)
                    break;
                if (edit == IndentEdit::Indent)
                    mapped += change.count;
                else
                    mapped -= (std::min)(change.count, offset - change.at);
            }
            return mapped;
        }

        IndentedText ReplaceSelection(StringView text, size_t begin, size_t end)
        {
            String result(text.substr(0, begin));
            result += IndentUnit;
            result += text.substr(end);
            const size_t cursor = begin + IndentUnit.size();
            return { std::move(result), cursor, cursor };
        }

    } // namespace

    StringView LineIndent(StringView text, size_t offset)
    {
        const size_t start = LineStart(text, (std::min)(offset, text.size()));
        const size_t end = text.find_first_not_of(" \t", start);
        return text.substr(start, (end == StringView::npos ? text.size() : end) - start);
    }

    IndentedText ApplyIndentEdit(StringView text, size_t begin, size_t end, IndentEdit edit)
    {
        end = (std::min)(end, text.size());
        begin = (std::min)(begin, end);
        if (edit == IndentEdit::None)
            return { String(text), begin, end };
        if (edit == IndentEdit::Indent && text.substr(begin, end - begin).find('\n') == StringView::npos)
            return ReplaceSelection(text, begin, end);

        const Vector<LineChange> changes = CollectChanges(text, begin, end, edit);
        String result;
        size_t copied = 0;
        for (const auto& change : changes) {
            result += text.substr(copied, change.at - copied);
            if (edit == IndentEdit::Indent) {
                result += IndentUnit;
                copied = change.at;
            } else {
                copied = change.at + change.count;
            }
        }
        result += text.substr(copied);
        return { std::move(result), MapOffset(begin, changes, edit), MapOffset(end, changes, edit) };
    }

} // namespace elm::script
