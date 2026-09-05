engine_add_library(graphics
    RenderSystem.hpp
    RenderSystem.cpp
    ImGuiSystem.cpp
    Camera.cpp
    CameraController.cpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.cpp    
    ui/DockSpaceView.cpp
    ui/EngineViewportWindow.cpp
    ui/SocLabWindow.cpp
    ui/DepthPreviewWindow.cpp
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui/backends/imgui_impl_glfw.cpp
)

target_link_libraries(graphics PUBLIC glfw Diligent-Imgui)

target_link_libraries(graphics PRIVATE core)

if(WIN32)
    target_link_libraries(graphics PUBLIC Diligent-GraphicsEngineD3D12-static)
else()
    target_link_libraries(graphics PUBLIC Diligent-GraphicsEngineVk-static)
endif()

target_include_directories(graphics PUBLIC
    ${ENGINE_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
)

target_include_directories(graphics SYSTEM PUBLIC
    ${diligentengine_SOURCE_DIR}/DiligentCore
    ${diligentengine_SOURCE_DIR}/DiligentTools
    ${diligentengine_SOURCE_DIR}/DiligentTools/ThirdParty/imgui
    ${diligentengine_SOURCE_DIR}/DiligentTools/Imgui/interface
)
