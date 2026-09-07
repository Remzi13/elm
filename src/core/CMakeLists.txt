engine_add_library(core
    Std.hpp
    Memory.hpp
    Memory.cpp
    Log.hpp
    Log.cpp
    Error.cpp
    Error.hpp
    Timer.hpp
    Timer.cpp
    Threading.hpp
    Threading.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
