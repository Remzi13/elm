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

engine_add_library(graphics
    RenderSystem.cpp
    Camera.cpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.cpp
    ../Scene/TestScenes.cpp
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui/backends/imgui_impl_glfw.cpp
)

target_link_libraries(graphics PUBLIC
    glfw
    Diligent-GraphicsEngineVk-static
    Diligent-Imgui
)

target_include_directories(graphics PUBLIC
    ${ENGINE_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
    ${diligentengine_SOURCE_DIR}/DiligentCore
    ${diligentengine_SOURCE_DIR}/DiligentTools
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui
    ${diligentengine_SOURCE_DIR}/DiligentTools/Imgui/interface
)
