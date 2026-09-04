#pragma once

#include "core/Memory.hpp"

#include <string>
#include <vector>

namespace elm {
    using String = std::basic_string<char, std::char_traits<char>, memory::Allocator<char>>;
    using StringView = std::basic_string_view<char, std::char_traits<char>>;

    template<typename T>
    using Vector = std::vector<T, memory::Allocator<T>>;
}
