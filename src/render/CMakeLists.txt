engine_add_library(render
    RenderSystem.hpp
    RenderSystem.cpp
    RenderPipeline.hpp
    RenderPipeline.cpp
    ViewPort.hpp
    OverlayFrame.hpp
    ShaderSource.hpp
    ShaderSource.cpp
    CommandList.hpp

    api/Handles.hpp
    api/HandleAllocator.hpp
    api/ResourceDesc.hpp
    api/ResourceCommands.hpp
    api/RenderResources.hpp
    api/RenderResources.cpp
    api/FrameWriter.hpp
    api/FrameWriter.cpp
    api/NativeWindow.hpp
    api/PipelineDesc.hpp

    frame/FrameSnapshot.hpp
    frame/DebugGeometry.hpp
    frame/FrameRing.hpp
    frame/FrameRing.cpp

    rhi/CommandList.hpp
    rhi/UploadAllocator.hpp
    rhi/UploadAllocator.cpp
    rhi/IRenderBackend.hpp

    graph/RenderGraph.hpp
    graph/RenderGraph.cpp

    passes/IRenderFeature.hpp
    passes/ScenePass.hpp
    passes/ScenePass.cpp
    passes/OverlayPass.hpp
    passes/OverlayPass.cpp
    passes/DebugDrawPass.hpp
    passes/DebugDrawPass.cpp
)

target_link_libraries(render PUBLIC core math)
target_include_directories(render PUBLIC ${ENGINE_INCLUDE_DIRS})

# ------------------------------------------------------------------------------
# Backend: Diligent (D3D12) on Windows, own Vulkan backend on Linux.
# Only the selected backend is compiled, its API stays private to render.
# ------------------------------------------------------------------------------
if(WIN32)
    target_sources(render PRIVATE
        backend/diligent/DiligentBackend.hpp
        backend/diligent/DiligentBackend.cpp
        backend/diligent/SwapChain.hpp
        backend/diligent/SwapChain.cpp
        backend/diligent/Utils.hpp
        backend/diligent/Utils.cpp
    )
    target_link_libraries(render PRIVATE Diligent-GraphicsEngineD3D12-static)
    target_include_directories(render SYSTEM PRIVATE
        ${diligentengine_SOURCE_DIR}/DiligentCore
    )
else()
    find_package(Vulkan 1.3 REQUIRED)
    target_sources(render PRIVATE
        backend/vulkan/VulkanBackend.hpp
        backend/vulkan/VulkanBackend.cpp
        backend/vulkan/VulkanCommandTranslator.hpp
        backend/vulkan/VulkanCommandTranslator.cpp
        backend/vulkan/VulkanDevice.hpp
        backend/vulkan/VulkanDevice.cpp
        backend/vulkan/VulkanFormats.hpp
        backend/vulkan/VulkanFormats.cpp
        backend/vulkan/VulkanFrame.hpp
        backend/vulkan/VulkanFrame.cpp
        backend/vulkan/VulkanMemory.hpp
        backend/vulkan/VulkanMemory.cpp
        backend/vulkan/VulkanPipeline.hpp
        backend/vulkan/VulkanPipeline.cpp
        backend/vulkan/VulkanShader.hpp
        backend/vulkan/VulkanShader.cpp
        backend/vulkan/VulkanSurface.hpp
        backend/vulkan/VulkanSurface.cpp
        backend/vulkan/VulkanSwapChain.hpp
        backend/vulkan/VulkanSwapChain.cpp
    )
    target_link_libraries(render PRIVATE Vulkan::Vulkan)
    elm_add_spirv_shaders(render
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders
        VARIANTS   "overlay.frag.hlsl:OVERLAY_MANUAL_SRGB=1"
    )
endif()
