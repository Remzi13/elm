#pragma once

#include "core/Std.hpp"

#include <expected>
#include <string_view>

namespace elm {

    enum class ErrorCode {
        WindowInitializationFailed,
        RenderEngineInitializationFailed,
        SerializationFailed,
        ScriptError,
        UnknownError,
        FileAccessFailed
    };

    struct EngineError {
        ErrorCode code { ErrorCode::UnknownError };
        String message;

        explicit EngineError(ErrorCode c, StringView msg = "")
            : code(c)
            , message(msg)
        {}
    };

    template <typename T>
    using EngineResult = std::expected<T, EngineError>;

} // namespace elm
