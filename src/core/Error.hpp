#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <format>

namespace Engine {

enum class ErrorCode {
    WindowInitializationFailed,
    RenderEngineInitializationFailed,
    PhysicsInitializationFailed,
    ResourceCreationFailed,
    UnknownError
};

struct EngineError {
    ErrorCode code{ErrorCode::UnknownError};
    std::string message;

    explicit EngineError(ErrorCode c, std::string_view msg = "")
        : code(c), message(msg) {}
};

template <typename T>
using EngineResult = std::expected<T, EngineError>;

} // namespace Engine
