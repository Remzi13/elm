#include "script/ui/LuaHighlighter.hpp"

#include <algorithm>
#include <array>

namespace elm::script {

    namespace {

        constexpr std::array<StringView, 19> Keywords = {
            "and", "break", "do", "else", "elseif", "end", "for", "function", "goto", "if",
            "in", "local", "not", "or", "repeat", "return", "then", "until", "while",
        };

        constexpr std::array<StringView, 3> Constants = { "nil", "true", "false" };

        constexpr std::array<StringView, 30> Builtins = {
            "assert", "collectgarbage", "error", "getmetatable", "ipairs", "load", "next", "pairs", "pcall",
            "print", "rawequal", "rawget", "rawlen", "rawset", "require", "select", "setmetatable",
            "tonumber", "tostring", "type", "xpcall", "coroutine", "math", "string", "table", "utf8",
            "package", "elm", "self", "_G",
        };

        template <size_t N>
        bool Contains(const std::array<StringView, N>& words, StringView word)
        {
            return std::find(words.begin(), words.end(), word) != words.end();
        }

        bool IsNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        bool IsDigit(char c) { return c >= '0' && c <= '9'; }
        bool IsHexDigit(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
        bool IsName(char c) { return IsNameStart(c) || IsDigit(c); }

        class Tokenizer {
        public:
            Tokenizer(StringView text, Vector<LuaSpan>& spans) : m_text(text), m_spans(spans) { }

            void Run()
            {
                while (m_pos < m_text.size()) {
                    const size_t start = m_pos;
                    const char c = m_text[m_pos];
                    if (c == '-' && Peek(1) == '-') {
                        Comment(start);
                    } else if (c == '[' && LongBracketLevel(m_pos) >= 0) {
                        m_pos = LongBracketEnd(m_pos);
                        Emit(LuaToken::String, start);
                    } else if (c == '"' || c == '\'') {
                        QuotedString(c);
                        Emit(LuaToken::String, start);
                    } else if (IsDigit(c) || (c == '.' && IsDigit(Peek(1)))) {
                        Number();
                        Emit(LuaToken::Number, start);
                    } else if (IsNameStart(c)) {
                        Name(start);
                    } else if (m_afterFunction && (c == '.' || c == ':')) {
                        // function a.b:c: the separators belong to the name
                        ++m_pos;
                        Emit(LuaToken::FunctionName, start);
                    } else {
                        // Anything else but spaces ends the name chain after `function`
                        if (c != ' ' && c != '\t')
                            m_afterFunction = false;
                        ++m_pos;
                        Emit(LuaToken::Text, start);
                    }
                }
            }

        private:
            [[nodiscard]] char Peek(size_t offset) const
            {
                return m_pos + offset < m_text.size() ? m_text[m_pos + offset] : '\0';
            }

            void Emit(LuaToken kind, size_t begin)
            {
                const auto spanBegin = static_cast<uint32_t>(begin);
                const auto spanEnd = static_cast<uint32_t>(m_pos);
                if (spanBegin == spanEnd)
                    return;
                if (!m_spans.empty() && m_spans.back().kind == kind && m_spans.back().end == spanBegin)
                    m_spans.back().end = spanEnd;
                else
                    m_spans.push_back({ spanBegin, spanEnd, kind });
            }

            /// [[ -> 0, [==[ -> 2, anything else -1.
            [[nodiscard]] int LongBracketLevel(size_t at) const
            {
                if (at >= m_text.size() || m_text[at] != '[')
                    return -1;
                size_t index = at + 1;
                while (index < m_text.size() && m_text[index] == '=')
                    ++index;
                return index < m_text.size() && m_text[index] == '[' ? static_cast<int>(index - at - 1) : -1;
            }

            /// End of the long bracket opened at `at`: after its ]=*], or the end of the text.
            [[nodiscard]] size_t LongBracketEnd(size_t at) const
            {
                const int level = LongBracketLevel(at);
                String closing = "]";
                closing.append(static_cast<size_t>(level), '=');
                closing += ']';
                const size_t found = m_text.find(StringView(closing), at + static_cast<size_t>(level) + 2);
                return found == StringView::npos ? m_text.size() : found + closing.size();
            }

            void Comment(size_t start)
            {
                m_pos += 2;
                if (LongBracketLevel(m_pos) >= 0) {
                    m_pos = LongBracketEnd(m_pos);
                } else {
                    const size_t lineEnd = m_text.find('\n', m_pos);
                    m_pos = lineEnd == StringView::npos ? m_text.size() : lineEnd;
                }
                Emit(LuaToken::Comment, start);
            }

            /// "..." or '...': escapes skip the next character, a line break ends an unfinished string.
            void QuotedString(char quote)
            {
                ++m_pos;
                while (m_pos < m_text.size() && m_text[m_pos] != quote && m_text[m_pos] != '\n')
                    m_pos += m_text[m_pos] == '\\' && m_pos + 1 < m_text.size() ? 2 : 1;
                if (m_pos < m_text.size() && m_text[m_pos] == quote)
                    ++m_pos;
            }

            void Number()
            {
                if (m_text[m_pos] == '0' && (Peek(1) == 'x' || Peek(1) == 'X')) {
                    m_pos += 2;
                    while (m_pos < m_text.size()) {
                        const char c = m_text[m_pos];
                        if (IsHexDigit(c) || c == '.')
                            ++m_pos;
                        else if ((c == 'p' || c == 'P') && (Peek(1) == '+' || Peek(1) == '-'))
                            m_pos += 2;
                        else if (c == 'p' || c == 'P')
                            ++m_pos;
                        else
                            break;
                    }
                    return;
                }
                while (m_pos < m_text.size()) {
                    const char c = m_text[m_pos];
                    if (IsDigit(c) || c == '.')
                        ++m_pos;
                    else if ((c == 'e' || c == 'E') && (Peek(1) == '+' || Peek(1) == '-'))
                        m_pos += 2;
                    else if (c == 'e' || c == 'E')
                        ++m_pos;
                    else
                        break;
                }
            }

            void Name(size_t start)
            {
                while (m_pos < m_text.size() && IsName(m_text[m_pos]))
                    ++m_pos;
                const StringView word = m_text.substr(start, m_pos - start);

                // The character before the name, spaces skipped: a field (x.print) is not the global
                size_t before = start;
                while (before > 0 && (m_text[before - 1] == ' ' || m_text[before - 1] == '\t'))
                    --before;
                const bool isField = before > 0 && (m_text[before - 1] == '.' || m_text[before - 1] == ':');

                if (Contains(Constants, word)) {
                    Emit(LuaToken::Constant, start);
                } else if (Contains(Keywords, word)) {
                    m_afterFunction = word == "function";
                    Emit(LuaToken::Keyword, start);
                } else if (m_afterFunction) {
                    Emit(LuaToken::FunctionName, start);
                } else if (!isField && Contains(Builtins, word)) {
                    Emit(LuaToken::Builtin, start);
                } else {
                    Emit(LuaToken::Text, start);
                }
            }

            StringView m_text;
            Vector<LuaSpan>& m_spans;
            size_t m_pos { 0 };
            /// Inside `function a.b:c`: the names are colored as the function name until the '('.
            bool m_afterFunction { false };
        };

    } // namespace

    void TokenizeLua(StringView text, Vector<LuaSpan>& spans)
    {
        spans.clear();
        Tokenizer(text, spans).Run();
    }

} // namespace elm::script
