#pragma once

#include <stdint.h>




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
    };
    
    void add( Severity severity, Category category, char const* pSourceInfo, char const* pFilename, int pLineNumber, char const* pMessageFormat, ... );
}

#define LOG_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::Info, category, source, __FILE__, __LINE__, __VA_ARGS__ )
#define ERORR_MESSAGE( category, source, ... ) elm::log::add( elm::log::Severity::Error, category, source, __FILE__, __LINE__, __VA_ARGS__ )