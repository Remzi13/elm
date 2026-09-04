#include "core/Log.hpp"

#include <cstdarg>

namespace elm::log {

    void add( Severity severity, Category category, char const* pSourceInfo, char const* pFilename, int pLineNumber, char const* pMessageFormat, ... )
    {
        va_list args;
        va_start( args, pMessageFormat );        
        va_end( args );
    }
}