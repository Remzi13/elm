#include "editor/script/ErrorLocation.hpp"

#include <charconv>

namespace elm::script {

    std::optional<ErrorLocation> FindErrorLocation(StringView message)
    {
        for (size_t found = message.find(".lua:"); found != StringView::npos; found = message.find(".lua:", found + 1)) {
            // The file name runs back to a space, a quote or the start of a line
            size_t begin = found;
            while (begin > 0 && message[begin - 1] != ' ' && message[begin - 1] != '\t' && message[begin - 1] != '\n'
                && message[begin - 1] != '\'' && message[begin - 1] != '"' && message[begin - 1] != '@')
                --begin;
            const size_t digits = found + 5;
            int line = 0;
            const auto [end, error] = std::from_chars(message.data() + digits, message.data() + message.size(), line);
            if (error == std::errc() && end != message.data() + digits && line > 0)
                return ErrorLocation { String(message.substr(begin, found + 4 - begin)), line };
        }
        return std::nullopt;
    }

} // namespace elm::script
