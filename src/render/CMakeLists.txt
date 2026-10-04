engine_add_library(render
    Render.hpp
    RenderSystem.hpp
    RenderSystem.cpp
    
    IRenderPass.hpp
    ViewPort.hpp
    OverlayFrame.hpp
    OverlayRenderPass.hpp
    OverlayRenderPass.cpp
    SceneRenderPass.hpp
    SceneRenderPass.cpp
    Render.hpp
    Command.hpp
    CommandList.hpp
    CommandQueue.hpp
    CommandQueue.cpp
    SwapChain.hpp
    SwapChain.cpp
    RenderResourceProvider.hpp
    RenderResourceProvider.cpp    
    BufferManager.hpp
    BufferManager.cpp
    Texture.hpp
    Texture.cpp
    TextureManager.hpp
    TextureManager.cpp
    TextureStore.hpp
    TextureStore.cpp
    ResourceView.hpp
    ResourceView.cpp
    MeshManager.h
    MeshManager.cpp
    DynamicLinearAllocator.hpp
    DynamicLinearAllocator.cpp
    backends/Utils.hpp
    backends/Utils.cpp    
)

target_link_libraries(render PUBLIC glfw)

target_link_libraries(render PRIVATE core)

if(WIN32)
    target_link_libraries(render PUBLIC Diligent-GraphicsEngineD3D12-static)
else()
    target_link_libraries(render PUBLIC Diligent-GraphicsEngineVk-static)
endif()

target_include_directories(render PUBLIC
    ${ENGINE_INCLUDE_DIRS}
    ${glfw_SOURCE_DIR}/include
)

target_include_directories(render SYSTEM PUBLIC
    ${diligentengine_SOURCE_DIR}/DiligentCore
    ${diligentengine_SOURCE_DIR}/DiligentTools
)
