cmake_minimum_required(VERSION 3.28)
project(EngineCore LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include custom compile options
include(cmake/CompileOptions.cmake)

# Enable FetchContent for dependencies
include(FetchContent)

# ------------------------------------------------------------------------------
# Dependency 1: GLFW 3.4
# ------------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ------------------------------------------------------------------------------
# Dependency 2: Jolt Physics Engine
# ------------------------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ------------------------------------------------------------------------------
# Dependency 3: Diligent Engine
# ------------------------------------------------------------------------------
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_NO_FORMAT_VALIDATION ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_RENDER_STATE_PACKAGER ON CACHE BOOL "" FORCE)


FetchContent_Declare(
    DiligentEngine
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentEngine.git
    GIT_TAG        v2.5.6
    PATCH_COMMAND  sed -i "s/add_subdirectory(RenderStateNotation)/#add_subdirectory(RenderStateNotation)/g" DiligentTools/CMakeLists.txt
)
FetchContent_MakeAvailable(DiligentEngine)

# ------------------------------------------------------------------------------
# Engine Core Target Setup
# ------------------------------------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES
    "Engine/*.cpp"
    "Engine/*.hpp"
    "src/main.cpp"
)

# Add the imgui GLFW backend implementation
list(APPEND ENGINE_SOURCES ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui/backends/imgui_impl_glfw.cpp)

add_executable(EngineCore ${ENGINE_SOURCES})

target_include_directories(EngineCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${glfw_SOURCE_DIR}/include
    ${diligentengine_SOURCE_DIR}/DiligentCore
    ${diligentengine_SOURCE_DIR}/DiligentTools
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui
    ${diligentengine_SOURCE_DIR}/DiligentTools/Imgui/interface
)

target_compile_definitions(EngineCore PUBLIC PLATFORM_LINUX=1)

target_set_warnings(EngineCore)

target_link_libraries(EngineCore PRIVATE
    glfw
    Jolt
    Diligent-GraphicsEngineVk-static
    Diligent-Imgui
)

set_target_properties(EngineCore PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
