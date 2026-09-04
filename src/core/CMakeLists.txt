engine_add_library(core
    Log.hpp
    Log.cpp
    Error.cpp
    Error.hpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
