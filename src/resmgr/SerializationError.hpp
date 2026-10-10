#pragma once

#include <string>
#include <utility>

#include "core/Unexpected.hpp"

namespace elm::resmgr {
    inline auto UnexpectedSerializationError(EngineError error)
        -> std::unexpected<EngineError>
    {
        return MakeUnexpected(std::move(error));
    }

    inline auto UnexpectedSerializationError(String message, const char* source)
        -> std::unexpected<EngineError>
    {
        return MakeUnexpected(ErrorCode::SerializationFailed, std::move(message), log::Category::Core, source);
    }

    inline auto UnexpectedSerializationError(StringView message, const char* source)
        -> std::unexpected<EngineError>
    {
        return UnexpectedSerializationError(String(message.begin(), message.end()), source);
    }

    inline auto UnexpectedSerializationError(const std::string& message, const char* source)
        -> std::unexpected<EngineError>
    {
        return UnexpectedSerializationError(StringView(message.data(), message.size()), source);
    }

    inline auto UnexpectedSerializationError(const char* message, const char* source)
        -> std::unexpected<EngineError>
    {
        return UnexpectedSerializationError(StringView(message), source);
    }
}
