engine_add_library(graphics
    Camera.hpp    
    Camera.cpp
    CameraController.hpp
    CameraController.cpp
    Model.cpp
    Model.hpp
    ModelHandler.hpp
    ModelManager.cpp
    ModelManager.hpp
    ModelImport.hpp
    ModelImport.cpp

    culling/SoftwareDepthBuffer.hpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.hpp
    culling/OcclusionCullingSystem.cpp
    ui/DockSpaceView.hpp
    ui/DockSpaceView.cpp
    ui/EditorStyle.hpp
    ui/EditorStyle.cpp
    ui/EngineViewportWindow.hpp
    ui/EngineViewportWindow.cpp
    ui/WindowMessages.hpp
    ui/FileDialog.hpp
    ui/FileDialog.cpp
    ui/LogWindow.hpp
    ui/LogWindow.cpp
    ui/SceneHierarchyWindow.hpp
    ui/SceneHierarchyWindow.cpp
    ui/SocLabWindow.hpp
    ui/SocLabWindow.cpp
    ui/ImGuiRenderer.hpp
    ui/ImGuiRenderer.cpp
    ui/ImGuiSystem.hpp
    ui/ImGuiSystem.cpp
    ui/DepthPreviewWindow.hpp
    ui/DepthPreviewWindow.cpp
    ui/ProfilerWindow.hpp
    ui/ProfilerWindow.cpp
    ui/SettingsWindow.hpp
    ui/SettingsWindow.cpp
    ui/ModelEditorWindow.hpp
    ui/ModelEditorWindow.cpp
)

target_link_libraries(graphics PUBLIC imgui_glfw imguizmo)
target_link_libraries(graphics PUBLIC core render platform resmgr)
target_link_libraries(graphics PRIVATE glfw)
