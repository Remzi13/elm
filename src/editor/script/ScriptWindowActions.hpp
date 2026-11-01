#pragma once

#include "core/Std.hpp"

#include <functional>

namespace elm::script {

    /// Kinds of lines in the console of the Scripts window.
    enum class ConsoleLineKind : uint8_t { Command, Output, Error, Info };

    /// How a part of the Scripts window reports something: a line in its console.
    using PrintLine = std::function<void(ConsoleLineKind kind, StringView text)>;
    /// How a part of the Scripts window opens a module in the Editor tab.
    using OpenModule = std::function<void(StringView module)>;
    /// How a part of the Scripts window opens the file and line an error message names.
    using OpenLocation = std::function<void(StringView message)>;

} // namespace elm::script
