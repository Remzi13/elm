#pragma once

#include "core/Std.hpp"

namespace elm::render {

    /// Loads a shader file from shaders/ (next to the binary first, then the source tree).
    /// Returns an empty string when the file is missing.
    [[nodiscard]] String loadShaderSource(StringView fileName);

} // namespace elm::render
