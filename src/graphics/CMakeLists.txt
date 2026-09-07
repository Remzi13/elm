engine_add_library(graphics
    RenderSystem.hpp
    RenderSystem.cpp
    backend/diligent/DiligentRenderer.hpp
    backend/diligent/DiligentRenderer.cpp
    RenderDevice.hpp
    RenderContext.hpp
    RenderFrame.hpp
    Renderer.hpp
    RenderBuffer.hpp
    RenderMesh.hpp
    RenderShader.hpp
    RenderMaterial.hpp
    RenderPipeline.hpp
    RenderResourceManager/Buffer.hpp
    RenderResourceManager/Texture.hpp
    RenderResourceManager/Mesh.hpp
    RenderResourceManager/Shader.hpp
    RenderResourceManager/Material.hpp
    RenderResourceManager/Pipeline.hpp
    RenderResourceManager.hpp
    RenderSurfaceManager.hpp
    RenderWorld.hpp
    SceneRenderer.hpp
    VisibilitySystem.hpp
    passes/VisibilityPass.hpp
    passes/DepthPass.hpp
    passes/OpaquePass.hpp
    passes/TransparentPass.hpp
    passes/DebugPass.hpp
    backend/diligent/DiligentRenderDevice.hpp
    backend/diligent/DiligentRenderContext.hpp
    backend/diligent/DiligentBuffer.hpp
    backend/diligent/DiligentTexture.hpp
    ImGuiRenderer.hpp
    ImGuiRenderer.cpp
    ImGuiSystem.cpp
    Camera.cpp
    CameraController.cpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.cpp    
    ui/DockSpaceView.cpp
    ui/EngineViewportWindow.cpp
    ui/SocLabWindow.cpp
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
