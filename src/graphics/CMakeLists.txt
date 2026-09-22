engine_add_library(graphics
    RenderSystem.hpp
    RenderSystem.cpp
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
    ui/SocLabWindow.hpp
    ui/SocLabWindow.cpp
    
    render/Render.hpp
    render/Command.hpp
    render/CommandList.hpp
    render/CommandQueue.hpp
    render/CommandQueue.cpp
    render/BufferManager.hpp
    render/BufferManager.cpp
    render/Texture.hpp
    render/Texture.cpp
    render/TextureManager.hpp
    render/TextureManager.cpp
    render/MeshManager.h
    render/MeshManager.cpp
    render/DynamicLinearAllocator.hpp
    render/DynamicLinearAllocator.cpp
    render/backends/Utils.hpp
    render/backends/Utils.cpp

    ui/DepthPreviewWindow.hpp
    ui/DepthPreviewWindow.cpp
)

target_link_libraries(graphics PUBLIC glfw Diligent-Imgui imgui_glfw)

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
