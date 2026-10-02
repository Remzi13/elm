#pragma once

#include "core/config.hpp"

#include "core/Std.hpp"


#include <stdint.h>
#include <chrono>

namespace elm::log {

    enum class Severity
    {
        Info = 0,
        Warning,
        Error,
        FatalError,
    };

    enum class Category : int8_t
    {
        Invalid = -1,
        Render,
        Physics,
        Core,
    };

    struct Entry
    {
        std::chrono::system_clock::time_point timestamp;
        Severity severity;
        Category category;
        String sourceInfo;
        String filename;
        int lineNumber;
        String message;
    };

    void add( Severity severity, Category category, char const* pSourceInfo, char const* pFilename, int pLineNumber, char const* pMessageFormat, ... );
    const Vector<Entry>& getMessages();
    Vector<Entry> takeMessages();
    void clearMessages();
    void setMaxMessages(std::size_t maxMessages);
    std::size_t getMaxMessages();
}

#define LOG_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::Info, category, source, __FILE__, __LINE__, __VA_ARGS__ )
#define WARNING_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::Warning, category, source, __FILE__, __LINE__, __VA_ARGS__ )
#define ERROR_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::Error, category, source, __FILE__, __LINE__, __VA_ARGS__ )
#define FATAL_ERROR_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::FatalError, category, source, __FILE__, __LINE__, __VA_ARGS__ )
#define ERORR_MESSAGE( category, source, ... ) ERROR_MESSAGE( category, source, __VA_ARGS__ )