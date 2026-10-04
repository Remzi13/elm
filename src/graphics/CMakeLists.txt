engine_add_library(graphics
    ImGuiSystem.cpp
    Camera.cpp
    CameraController.cpp
    Settings.hpp
    Settings.cpp
    MeshDataStorage.hpp
    MeshDataStorage.cpp

    culling/SoftwareDepthBuffer.hpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.hpp
    culling/OcclusionCullingSystem.cpp
    ui/DockSpaceView.hpp
    ui/DockSpaceView.cpp
    ui/EngineViewportWindow.hpp
    ui/EngineViewportWindow.cpp
    ui/WindowMessages.hpp
    ui/LogWindow.hpp
    ui/LogWindow.cpp
    ui/SceneHierarchyWindow.hpp
    ui/SceneHierarchyWindow.cpp
    ui/SocLabWindow.hpp
    ui/SocLabWindow.cpp
    ui/ImGuiRenderer.hpp
    ui/ImGuiRenderer.cpp

    ui/DepthPreviewWindow.hpp
    ui/DepthPreviewWindow.cpp
    ui/ProfilerWindow.hpp
    ui/ProfilerWindow.cpp
)

target_link_libraries(graphics PUBLIC imgui_glfw imguizmo)
target_link_libraries(graphics PRIVATE core render)
