engine_add_library(render
    Render.hpp
    Render.cpp
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
# Backend: native Direct3D 12 on Windows, own Vulkan backend on Linux.
# Only the selected backend is compiled, its API stays private to render.
# ------------------------------------------------------------------------------
if(WIN32)
    target_sources(render PRIVATE
        backend/dx12/Dx12Backend.hpp
        backend/dx12/Dx12Backend.cpp
    )
    target_link_libraries(render PRIVATE d3d12 dxgi dxguid)
    elm_add_dxil_shaders(render
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders
        VARIANTS "overlay.frag.hlsl:OVERLAY_MANUAL_SRGB=1"
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
