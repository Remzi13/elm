engine_add_library(core
    config.hpp
    Std.hpp
    Debug.hpp
    Memory.hpp
    Memory.cpp
    Log.hpp
    Log.cpp
    Error.hpp
    Unexpected.hpp
    FrameClock.hpp
    FrameClock.cpp
    Timer.hpp
    Timer.cpp
    Threading.hpp
    Threading.cpp
    JobSystem.hpp
    JobSystem.cpp
    Handler.hpp
    Profiling.hpp
    Settings.hpp
    Settings.cpp
    FileSystem.hpp
    FileSystem.cpp
    FilesystemPath.hpp
    BinaryBlock.hpp
    BinaryBlock.cpp
    File.hpp
    File.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(ELM_LOG_CONSOLE)
    target_compile_definitions(core PRIVATE ELM_LOG_CONSOLE=1)
endif()

if(TARGET TracyClient)
    target_link_libraries(core PUBLIC TracyClient)
endif()
