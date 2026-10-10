engine_add_library(core
    config.hpp
    Std.hpp
    Debug.hpp
    Memory.hpp
    Memory.cpp
    Log.hpp
    Log.cpp
    Error.cpp
    Error.hpp
    Unexpected.hpp
    Timer.hpp
    Timer.cpp
    Threading.hpp
    Threading.cpp
    JobSystem.hpp
    JobSystem.cpp
    Handler.hpp
    Handler.cpp
    Profiling.hpp
    MessageBus.hpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(TARGET TracyClient)
    target_link_libraries(core PUBLIC TracyClient)
endif()
