# Keep fetched sources outside the project tree to avoid AI indexing.
# Dependencies will be placed in a sibling directory: ../elm_deps
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/../elm_deps" CACHE PATH "FetchContent dependency directory" FORCE)
set(CMAKE_FOLDER "third party")

# ------------------------------------------------------------------------------
# Glaze
# ------------------------------------------------------------------------------
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG        v4.4.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glaze)
set_property(TARGET glaze_glaze PROPERTY FOLDER "third party")

function(set_dependency_folder directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        set_property(TARGET "${target}" PROPERTY FOLDER "third party")
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        set_dependency_folder("${subdirectory}")
    endforeach()
endfunction()

# ------------------------------------------------------------------------------
# GLFW 3.4
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
# Diligent Engine
# ------------------------------------------------------------------------------
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_NO_FORMAT_VALIDATION ON CACHE BOOL "" FORCE)
set(DILIGENT_NO_RENDER_STATE_PACKAGER ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    DiligentEngine
    GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentEngine.git
    GIT_TAG        v2.5.6
    PATCH_COMMAND  git -C <SOURCE_DIR>/DiligentTools/ThirdParty/imgui fetch --depth 1 origin refs/tags/v1.89.7-docking
                   COMMAND git -C <SOURCE_DIR>/DiligentTools/ThirdParty/imgui checkout --detach FETCH_HEAD
                   COMMAND ${CMAKE_COMMAND}
                       -DDILIGENT_TOOLS_CMAKE=<SOURCE_DIR>/DiligentTools/CMakeLists.txt
                       -P ${CMAKE_SOURCE_DIR}/cmake/DisableDiligentRenderStateNotation.cmake
)
FetchContent_MakeAvailable(DiligentEngine)
set_property(TARGET Diligent-Imgui PROPERTY FOLDER "third party")

# ImGuizmo
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        1.83
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imguizmo)

add_library(imguizmo STATIC
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.h
)
target_include_directories(imguizmo SYSTEM PUBLIC
    ${imguizmo_SOURCE_DIR}
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui
)
target_compile_definitions(imguizmo PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imguizmo PUBLIC Diligent-Imgui)
set_property(TARGET imguizmo PROPERTY FOLDER "third party")

# ImGui GLFW backend
add_library(imgui_glfw STATIC
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui/backends/imgui_impl_glfw.cpp
)

target_include_directories(imgui_glfw PRIVATE
    ${glfw_SOURCE_DIR}/include
    ${diligentengine_SOURCE_DIR}/DiligentCore
    ${diligentengine_SOURCE_DIR}/DiligentTools
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui
    ${diligentengine_SOURCE_DIR}/DiligentTools/Imgui/interface
)

target_link_libraries(imgui_glfw PRIVATE glfw Diligent-Imgui)
set_property(TARGET imgui_glfw PROPERTY FOLDER "third party")

# ------------------------------------------------------------------------------
# Jolt Physics Engine
# ------------------------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

if(MSVC)
    add_compile_options(/wd4865)
endif()

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ------------------------------------------------------------------------------
# Tracy Profiler
# ------------------------------------------------------------------------------
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG        v0.11.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tracy)

# Build the Tracy Profiler GUI executable (tracy-profiler) in dependencies
if(EXISTS "${tracy_SOURCE_DIR}/profiler")
    set(NO_PARALLEL_STL ON CACHE BOOL "" FORCE)
    set(LEGACY ON CACHE BOOL "" FORCE)
    # Profiler vendors old dependencies (freetype requires CMake 3.0), CMake 4 rejects policies < 3.5
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(${tracy_SOURCE_DIR}/profiler ${CMAKE_BINARY_DIR}/tracy-profiler)
    # Tracy 0.11.1 uses std::chrono without including <chrono>, recent MSVC STL no longer pulls it transitively
    # (per-source: Visual Studio generators cannot split target compile options by language, and the target has C sources)
    if(MSVC AND TARGET tracy-profiler)
        get_target_property(tracy_profiler_sources tracy-profiler SOURCES)
        list(FILTER tracy_profiler_sources INCLUDE REGEX "\\.cpp$")
        list(TRANSFORM tracy_profiler_sources REPLACE "^([^/:][^:]*)$" "${tracy_SOURCE_DIR}/profiler/\\1")
        set_property(SOURCE ${tracy_profiler_sources}
            DIRECTORY ${tracy_SOURCE_DIR}/profiler
            APPEND PROPERTY COMPILE_OPTIONS /FIchrono)
    endif()
endif()




set_dependency_folder("${glfw_SOURCE_DIR}")
set_dependency_folder("${diligentengine_SOURCE_DIR}")
set_dependency_folder("${JoltPhysics_SOURCE_DIR}")

# Expose dependency source directories to the engine modules.
set(glfw_SOURCE_DIR "${glfw_SOURCE_DIR}" PARENT_SCOPE)
set(diligentengine_SOURCE_DIR "${diligentengine_SOURCE_DIR}" PARENT_SCOPE)
set(JoltPhysics_SOURCE_DIR "${JoltPhysics_SOURCE_DIR}" PARENT_SCOPE)
set(tracy_SOURCE_DIR "${tracy_SOURCE_DIR}" PARENT_SCOPE)
