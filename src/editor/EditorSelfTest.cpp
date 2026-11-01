#include "editor/EditorSelfTest.hpp"

#include "editor/script/CodeIndent.hpp"
#include "editor/script/ErrorLocation.hpp"
#include "editor/script/LuaHighlighter.hpp"

#include <iostream>
#include <string>

namespace elm::editor {

    namespace {

        using namespace script;

        // ── Reporting ───────────────────────────────────────────────────────────

        struct Report {
            int passed { 0 };
            int failed { 0 };

            void Check(bool condition, StringView name, StringView detail = { })
            {
                if (condition) {
                    ++passed;
                    std::cout << "[PASS] " << name << "\n";
                } else {
                    ++failed;
                    std::cout << "[FAIL] " << name << (detail.empty() ? "" : ": ") << detail << "\n";
                }
            }
        };

        // ── Syntax coloring ─────────────────────────────────────────────────────

        /// "kind:text" for every span that is not plain text, to compare tokenizer output.
        String DescribeSpans(StringView source)
        {
            Vector<LuaSpan> spans;
            TokenizeLua(source, spans);
            static constexpr const char* names[] = { "text", "kw", "const", "str", "num", "comment", "builtin", "fn" };
            String described;
            uint32_t expected = 0;
            for (const auto& span : spans) {
                // The spans must cover the text in order
                if (span.begin != expected)
                    return "gap at " + String(std::to_string(span.begin));
                expected = span.end;
                if (span.kind == LuaToken::Text)
                    continue;
                described += String(names[static_cast<int>(span.kind)]) + ":" + String(source.substr(span.begin, span.end - span.begin)) + " ";
            }
            if (expected != source.size())
                return "not covered";
            return described;
        }

        void RunHighlighterTests(Report& report)
        {
            auto check = [&](const char* name, StringView source, StringView expected) {
                const String actual = DescribeSpans(source);
                report.Check(actual == expected, name, ("got: " + actual).c_str());
            };
            check("highlight::keywords_and_names", "local x = nil if not y then return true end",
                "kw:local const:nil kw:if kw:not kw:then kw:return const:true kw:end ");
            check("highlight::function_names", "function M.update(dt) end local function helper() end",
                "kw:function fn:M.update kw:end kw:local kw:function fn:helper kw:end ");
            check("highlight::builtins_not_fields", "print(elm.math.pi, t.print, self)",
                "builtin:print builtin:elm builtin:self ");
            check("highlight::strings", R"(a = "x\"y" .. 'z' .. [[long
line]] .. [==[a]]b]==])",
                "str:\"x\\\"y\" str:'z' str:[[long\nline]] str:[==[a]]b]==] ");
            check("highlight::unfinished_string_ends_at_line", "s = \"open\nx = 1",
                "str:\"open num:1 ");
            check("highlight::comments", "x = 1 -- note\n--[[ block\ncomment ]] y = 2 --[=[ ]] ]=]",
                "num:1 comment:-- note comment:--[[ block\ncomment ]] num:2 comment:--[=[ ]] ]=] ");
            check("highlight::unterminated_long_comment", "--[[ open\nstill comment",
                "comment:--[[ open\nstill comment ");
            check("highlight::numbers", "a = 3 + 0x1F + 1.5e-3 + .5 + 0x1p4 + 2E10",
                "num:3 num:0x1F num:1.5e-3 num:.5 num:0x1p4 num:2E10 ");
            check("highlight::operators_are_text", "a.b:c(d) ~= e // 2", "num:2 ");
            check("highlight::empty", "", "");
        }

        // ── Editor indentation ──────────────────────────────────────────────────

        /// The edited text with the selection marked: "[" at its begin, "]" at its end (the cursor).
        String DescribeIndent(StringView text, size_t begin, size_t end, IndentEdit edit)
        {
            const IndentedText edited = ApplyIndentEdit(text, begin, end, edit);
            String described = edited.text;
            described.insert(edited.selectionEnd, "]");
            described.insert(edited.selectionBegin, "[");
            return described;
        }

        void RunCodeIndentTests(Report& report)
        {
            auto check = [&](const char* name, const String& actual, StringView expected) {
                report.Check(actual == expected, name, ("got: " + actual).c_str());
            };
            check("indent::tab_at_cursor", DescribeIndent("ab", 1, 1, IndentEdit::Indent), "a    []b");
            check("indent::tab_replaces_selection_in_a_line", DescribeIndent("abc", 1, 2, IndentEdit::Indent), "a    []c");
            check("indent::selected_lines_skip_empty", DescribeIndent("a\n\nb\nc", 0, 4, IndentEdit::Indent), "[    a\n\n    b]\nc");
            check("indent::selection_after_newline_leaves_next_line", DescribeIndent("a\nb\n", 0, 2, IndentEdit::Indent), "[    a\n]b\n");
            check("indent::outdent_one_level_or_tab", DescribeIndent("      a\n\tb\nc", 0, 12, IndentEdit::Outdent), "[  a\nb\nc]");
            check("indent::outdent_cursor_in_indentation", DescribeIndent("    x", 2, 2, IndentEdit::Outdent), "[]x");
            check("indent::line_indent", String(LineIndent("  a\n\t b", 6)), "\t ");
        }

        // ── Error locations ─────────────────────────────────────────────────────

        void RunErrorLocationTests(Report& report)
        {
            const auto syntax = FindErrorLocation("demo/spinner.lua:12: unexpected symbol near '='");
            const auto loading = FindErrorLocation("error loading module 'main':\n\tmain.lua:3: <eof> expected");
            const auto runtime = FindErrorLocation("on_update: [string \"x\"]:1: oops\nstack traceback:\n\tgame/player.lua:40: in function");
            report.Check(syntax && syntax->file == "demo/spinner.lua" && syntax->line == 12
                    && loading && loading->file == "main.lua" && loading->line == 3
                    && runtime && runtime->file == "game/player.lua" && runtime->line == 40
                    && !FindErrorLocation("no location here.lua: at all"),
                "editor::error_location");
        }

    } // namespace

    int RunSelfTests()
    {
        Report report;
        RunHighlighterTests(report);
        RunCodeIndentTests(report);
        RunErrorLocationTests(report);
        std::cout << "\nEditor self tests: " << report.passed << " passed, " << report.failed << " failed" << std::endl;
        return report.failed;
    }

} // namespace elm::editor
