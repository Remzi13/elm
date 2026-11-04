#pragma once

#include "core/Error.hpp"
#include "core/Log.hpp"

#include <expected>
#include <source_location>
#include <utility>

namespace elm {

    inline auto MakeUnexpected(EngineError error) -> std::unexpected<EngineError>
    {
        return std::unexpected(std::move(error));
    }

    /// Writes the message to the log, with the place of the call.
    inline auto MakeUnexpected(ErrorCode code, StringView message, log::Category category, const char* source, std::source_location location = std::source_location::current())
        -> std::unexpected<EngineError>
    {
        log::add(log::Severity::Error, category, source, location.file_name(), static_cast<int>(location.line()), "%.*s", static_cast<int>(message.size()), message.data());
        return std::unexpected(EngineError(code, message));
    }

} // namespace elm
