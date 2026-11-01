#pragma once

#include "core/Std.hpp"

#include <optional>

namespace elm::script {

    /// Where a Lua message points: "demo/spinner.lua:12: ..." -> { "demo/spinner.lua", 12 }.
    struct ErrorLocation {
        String file;
        int line { 0 };
    };

    /// The first file:line in a Lua message, if it has one.
    [[nodiscard]] std::optional<ErrorLocation> FindErrorLocation(StringView message);

} // namespace elm::script
