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
    api/NativeWindow.hpp
    api/PipelineDesc.hpp

    frame/FrameSnapshot.hpp
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
)

target_link_libraries(render PUBLIC core math)
target_include_directories(render PUBLIC ${ENGINE_INCLUDE_DIRS})

# ------------------------------------------------------------------------------
# Backend: only the selected backend is compiled, its API stays private to render
# ------------------------------------------------------------------------------
#if(ELM_RENDER_BACKEND STREQUAL "Diligent")
    target_sources(render PRIVATE
        backend/diligent/DiligentBackend.hpp
        backend/diligent/DiligentBackend.cpp
        backend/diligent/SwapChain.hpp
        backend/diligent/SwapChain.cpp
        backend/diligent/Utils.hpp
        backend/diligent/Utils.cpp
    )
    if(WIN32)
        target_link_libraries(render PRIVATE Diligent-GraphicsEngineD3D12-static)
    else()
        target_link_libraries(render PRIVATE Diligent-GraphicsEngineVk-static)
    endif()
    target_include_directories(render SYSTEM PRIVATE
        ${diligentengine_SOURCE_DIR}/DiligentCore
    )
#elseif(ELM_RENDER_BACKEND STREQUAL "Null")
#    target_sources(render PRIVATE
#        backend/null/NullBackend.hpp
#        backend/null/NullBackend.cpp
#    )
#else()
#    message(FATAL_ERROR "Unknown ELM_RENDER_BACKEND '${ELM_RENDER_BACKEND}' (expected Diligent or Null)")
#endif()
