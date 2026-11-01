#pragma once

#include "core/Std.hpp"

namespace elm::script {

    enum class LuaToken : uint8_t {
        Text,
        Keyword,
        /// nil, true, false
        Constant,
        String,
        Number,
        Comment,
        /// Standard globals and the engine API root: print, pairs, string, math, elm, self, ...
        Builtin,
        /// The name after `function`: function M.update, local function helper
        FunctionName,
    };

    /// A run of text of one kind, byte offsets into the source.
    struct LuaSpan {
        uint32_t begin { 0 };
        uint32_t end { 0 };
        LuaToken kind { LuaToken::Text };
    };

    /// Splits Lua source into spans for syntax coloring. The spans are in order and cover the
    /// whole text; neighbors of the same kind are merged. Long strings and comments ([[ ]], [==[ ]==])
    /// may span lines; an unterminated one runs to the end of the text, as Lua would read it.
    void TokenizeLua(StringView text, Vector<LuaSpan>& spans);

} // namespace elm::script
