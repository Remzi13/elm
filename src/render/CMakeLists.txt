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
set(ELM_RENDER_BACKEND "Vulkan" CACHE STRING "Render backend (Vulkan, Null)")
set_property(CACHE ELM_RENDER_BACKEND PROPERTY STRINGS Vulkan Null)

if(ELM_RENDER_BACKEND STREQUAL "Vulkan")
    find_package(Vulkan REQUIRED)
    target_sources(render PRIVATE
        backend/vulkan/VulkanBackend.hpp
        backend/vulkan/VulkanBackend.cpp
    )
    target_link_libraries(render PRIVATE Vulkan::Vulkan)
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(render PRIVATE X11::X11)
        find_library(WAYLAND_CLIENT_LIB wayland-client)
        if(WAYLAND_CLIENT_LIB)
            target_link_libraries(render PRIVATE ${WAYLAND_CLIENT_LIB})
        endif()
    endif()

    # DirectXShaderCompiler (HLSL → SPIR-V)
    set(ELM_DXC_VERSION "v1.8.2505")
    set(ELM_DXC_DATE "2025_05_24")
    if(WIN32)
        set(ELM_DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${ELM_DXC_VERSION}/dxc_${ELM_DXC_DATE}.zip")
    else()
        set(ELM_DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${ELM_DXC_VERSION}/linux_dxc_${ELM_DXC_DATE}.x86_64.tar.gz")
    endif()
    FetchContent_Declare(dxc URL ${ELM_DXC_URL} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(dxc)
    if(WIN32)
        set(ELM_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
        set(ELM_DXC_LIB_DIR "${dxc_SOURCE_DIR}/bin/x64")
    else()
        set(ELM_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/dxc")
        set(ELM_DXC_LIB_DIR "${dxc_SOURCE_DIR}/lib")
        file(CHMOD ${ELM_DXC_EXECUTABLE} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
    target_compile_definitions(render PRIVATE
        ELM_DXC_PATH="${ELM_DXC_EXECUTABLE}"
        ELM_DXC_LIB_DIR="${ELM_DXC_LIB_DIR}"
    )
elseif(ELM_RENDER_BACKEND STREQUAL "Null")
    target_sources(render PRIVATE
        backend/null/NullBackend.hpp
        backend/null/NullBackend.cpp
    )
else()
    message(FATAL_ERROR "Unknown ELM_RENDER_BACKEND '${ELM_RENDER_BACKEND}' (expected Vulkan or Null)")
endif()

message(STATUS "ELM render backend: ${ELM_RENDER_BACKEND}")
