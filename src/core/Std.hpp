#pragma once

#include "core/Memory.hpp"

#include <string>

namespace elm {
    using String = std::basic_string<char, std::char_traits<char>, memory::Allocator<char>>;
    using StringView = std::basic_string_view<char, std::char_traits<char>>;
}
