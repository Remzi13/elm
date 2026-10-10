#pragma once

#include "core/Error.hpp"
#include "core/Log.hpp"

#include <expected>
#include <string>
#include <utility>

namespace elm {

inline auto MakeUnexpected(EngineError error) -> std::unexpected<EngineError>
{
    return std::unexpected(std::move(error));
}

inline auto MakeUnexpected(ErrorCode code, String message, log::Category category, const char* source)
    -> std::unexpected<EngineError>
{
    ERROR_MESSAGE(category, source, "%s", message.c_str());
    return std::unexpected(EngineError(code, StringView(message.data(), message.size())));
}

inline auto MakeUnexpected(ErrorCode code, StringView message, log::Category category, const char* source)
    -> std::unexpected<EngineError>
{
    return MakeUnexpected(code, String(message.begin(), message.end()), category, source);
}

inline auto MakeUnexpected(ErrorCode code, const std::string& message, log::Category category, const char* source)
    -> std::unexpected<EngineError>
{
    return MakeUnexpected(code, StringView(message.data(), message.size()), category, source);
}

inline auto MakeUnexpected(ErrorCode code, const char* message, log::Category category, const char* source)
    -> std::unexpected<EngineError>
{
    return MakeUnexpected(code, StringView(message), category, source);
}

} // namespace elm
