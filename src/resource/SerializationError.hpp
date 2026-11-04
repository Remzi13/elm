#pragma once

#include <source_location>
#include <utility>

#include "core/Unexpected.hpp"

namespace elm::resource {
    inline auto UnexpectedSerializationError(EngineError error)
        -> std::unexpected<EngineError>
    {
        return MakeUnexpected(std::move(error));
    }

    inline auto UnexpectedSerializationError(StringView message, const char* source, std::source_location location = std::source_location::current())
        -> std::unexpected<EngineError>
    {
        return MakeUnexpected(ErrorCode::SerializationFailed, message, log::Category::Core, source, location);
    }
} // namespace elm::resource
