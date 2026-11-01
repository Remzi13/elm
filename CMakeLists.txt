cmake_minimum_required(VERSION 3.28)
project(elm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ELM_LOG_CONSOLE "Duplicate log messages to the system console" ON)
option(ELM_BUILD_TRACY_PROFILER "Build the Tracy Profiler GUI (tracy-profiler) with the engine" ON)

# Include custom compile options
include(cmake/CompileOptions.cmake)
include(cmake/Shaders.cmake)

# Enable FetchContent for dependencies
include(FetchContent)

# External dependencies
add_subdirectory(third_party)

# ------------------------------------------------------------------------------
# Shared include directories for engine modules
# ------------------------------------------------------------------------------
set(ENGINE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------------------
# Common helper for engine libraries
# ------------------------------------------------------------------------------
function(engine_add_library target_name)
    add_library(${target_name} STATIC ${ARGN})
    target_include_directories(${target_name} PUBLIC ${ENGINE_INCLUDE_DIRS})
    if(WIN32)
        target_compile_definitions(${target_name} PUBLIC PLATFORM_WIN32=1)
    else()
        target_compile_definitions(${target_name} PUBLIC PLATFORM_LINUX=1)
    endif()
    target_set_warnings(${target_name})
endfunction()

# Common helper for the programs: elm, elm_editor, elm_tests
function(engine_add_executable target_name)
    add_executable(${target_name} ${ARGN})
    target_include_directories(${target_name} PUBLIC ${ENGINE_INCLUDE_DIRS})
    if(WIN32)
        target_compile_definitions(${target_name} PUBLIC PLATFORM_WIN32=1)
    else()
        target_compile_definitions(${target_name} PUBLIC PLATFORM_LINUX=1)
    endif()
    target_set_warnings(${target_name})
endfunction()

include_directories(src)

# ------------------------------------------------------------------------------
# Engine module targets
# ------------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/math)
add_subdirectory(src/input)
add_subdirectory(src/render)
add_subdirectory(src/Scene)
add_subdirectory(src/project)
add_subdirectory(src/resmgr)
add_subdirectory(src/graphics)
add_subdirectory(src/physics)
add_subdirectory(src/platform)
add_subdirectory(src/script)
add_subdirectory(src/engine)
add_subdirectory(src/editor)

# ------------------------------------------------------------------------------
# Programs
# ------------------------------------------------------------------------------
# elm (src/engine/main.cpp) and elm_editor (src/editor/main.cpp) are made by their modules

# Self tests: binding checks and Lua tests without a window or GPU (CI)
engine_add_executable(elm_tests
    src/tests/main.cpp
)
target_link_libraries(elm_tests PRIVATE core project resmgr script)

