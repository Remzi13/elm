cmake_minimum_required(VERSION 3.28)
project(Engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include custom compile options
include(cmake/CompileOptions.cmake)

# Enable FetchContent for dependencies
include(FetchContent)

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
    target_compile_definitions(${target_name} PUBLIC PLATFORM_LINUX=1)
    target_set_warnings(${target_name})
endfunction()

include_directories(src)

# ------------------------------------------------------------------------------
# Engine module targets
# ------------------------------------------------------------------------------
add_subdirectory(src/graphics)
add_subdirectory(src/physics)
add_subdirectory(src/core)

# ------------------------------------------------------------------------------
# Application target
# ------------------------------------------------------------------------------
add_executable(Engine
    src/main.cpp
)

target_include_directories(Engine PUBLIC ${ENGINE_INCLUDE_DIRS})
target_compile_definitions(Engine PUBLIC PLATFORM_LINUX=1)
target_set_warnings(Engine)
target_link_libraries(Engine PRIVATE core )
target_link_libraries(Engine PRIVATE Diligent-Imgui)

set_target_properties(Engine PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            ${CMAKE_BINARY_DIR}/shaders
)
