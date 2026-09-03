# Keep fetched sources and their generated build projects under third_party.
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "FetchContent dependency directory" FORCE)
set(CMAKE_FOLDER "third party")

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
    PATCH_COMMAND  git -C DiligentTools/ThirdParty/imgui fetch --depth 1 origin refs/tags/v1.89.7-docking
                   COMMAND git -C DiligentTools/ThirdParty/imgui checkout --detach FETCH_HEAD
                   COMMAND ${CMAKE_COMMAND}
                       -DDILIGENT_TOOLS_CMAKE=<SOURCE_DIR>/DiligentTools/CMakeLists.txt
                       -P ${CMAKE_SOURCE_DIR}/cmake/DisableDiligentRenderStateNotation.cmake
)
FetchContent_MakeAvailable(DiligentEngine)
set_property(TARGET Diligent-Imgui PROPERTY FOLDER "third party")

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

set_dependency_folder("${glfw_SOURCE_DIR}")
set_dependency_folder("${diligentengine_SOURCE_DIR}")
set_dependency_folder("${JoltPhysics_SOURCE_DIR}")

# Expose dependency source directories to the engine modules.
set(glfw_SOURCE_DIR "${glfw_SOURCE_DIR}" PARENT_SCOPE)
set(diligentengine_SOURCE_DIR "${diligentengine_SOURCE_DIR}" PARENT_SCOPE)
set(JoltPhysics_SOURCE_DIR "${JoltPhysics_SOURCE_DIR}" PARENT_SCOPE)
