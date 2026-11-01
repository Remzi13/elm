# HLSL shaders compiled to SPIR-V at build time for the Vulkan backend.
#
# Every <name>.vert.hlsl / <name>.frag.hlsl of SOURCE_DIR becomes SOURCE_DIR/spirv/<name>.<stage>.spv.
# A variant "<file>:<MACRO>=<VALUE>[,...]" is the same file compiled with those macros, written as
# <file without .hlsl>.<MACRO>_<VALUE>.spv (the name the backend builds from ShaderDesc::macros).
# The output lives next to the sources: shaders are found through the engine root, as the HLSL is.
#
# DXC is a pinned release fetched by third_party (ELM_DXC_FETCHED_EXECUTABLE);
# ELM_DXC_EXECUTABLE replaces it with an own one.

set(ELM_DXC_EXECUTABLE "" CACHE FILEPATH "Own DirectX Shader Compiler instead of the fetched release")

# Shared HLSL helpers that are not language intrinsics.
set(ELM_DXC_COMPAT_DEFINES
    "-DMatrixFromRows(r0,r1,r2,r3)=float4x4(r0,r1,r2,r3)"
)

function(elm_spirv_profile source out_profile)
    if(source MATCHES "\\.vert\\.hlsl$")
        set(${out_profile} vs_6_0 PARENT_SCOPE)
    elseif(source MATCHES "\\.frag\\.hlsl$")
        set(${out_profile} ps_6_0 PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Shader '${source}' must end with .vert.hlsl or .frag.hlsl")
    endif()
endfunction()

# The compiler the shader commands run: an own one when set, the fetched release otherwise.
function(elm_dxc_executable out_path)
    if(ELM_DXC_EXECUTABLE)
        set(${out_path} ${ELM_DXC_EXECUTABLE} PARENT_SCOPE)
    else()
        set(${out_path} ${ELM_DXC_FETCHED_EXECUTABLE} PARENT_SCOPE)
    endif()
endfunction()

# Adds a DXC command for one shader and appends its output to out_list.
function(elm_compile_spirv source_dir file macros out_list)
    elm_dxc_executable(dxc)
    elm_spirv_profile(${file} profile)
    string(REGEX REPLACE "\\.hlsl$" "" output_name ${file})
    set(defines)
    foreach(macro IN LISTS macros)
        string(REPLACE "=" "_" suffix ${macro})
        string(APPEND output_name ".${suffix}")
        list(APPEND defines "-D${macro}")
    endforeach()

    set(output ${source_dir}/spirv/${output_name}.spv)
    add_custom_command(
        OUTPUT  ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${source_dir}/spirv
        COMMAND ${dxc} -spirv -fspv-target-env=vulkan1.3 -T ${profile} -E main
                ${ELM_DXC_COMPAT_DEFINES} ${defines} -Fo ${output} ${source_dir}/${file}
        DEPENDS ${source_dir}/${file} ${dxc}
        COMMENT "SPIR-V ${output_name}.spv"
        VERBATIM
    )
    set(${out_list} ${${out_list}} ${output} PARENT_SCOPE)
endfunction()

# elm_add_spirv_shaders(<target> SOURCE_DIR <dir> [VARIANTS "<file>:<MACRO>=<VALUE>" ...])
function(elm_add_spirv_shaders target)
    cmake_parse_arguments(ARG "" "SOURCE_DIR" "VARIANTS" ${ARGN})
    if(NOT ELM_DXC_EXECUTABLE AND NOT ELM_DXC_FETCHED_EXECUTABLE)
        message(FATAL_ERROR "No DXC: the fetched release is missing and ELM_DXC_EXECUTABLE is not set")
    endif()

    file(GLOB sources RELATIVE ${ARG_SOURCE_DIR} CONFIGURE_DEPENDS ${ARG_SOURCE_DIR}/*.hlsl)
    set(outputs)
    foreach(file IN LISTS sources)
        elm_compile_spirv(${ARG_SOURCE_DIR} ${file} "" outputs)
    endforeach()
    foreach(variant IN LISTS ARG_VARIANTS)
        string(REPLACE ":" ";" parts ${variant})
        list(GET parts 0 file)
        list(GET parts 1 macros)
        string(REPLACE "," ";" macros ${macros})
        elm_compile_spirv(${ARG_SOURCE_DIR} ${file} "${macros}" outputs)
    endforeach()

    add_custom_target(${target}_spirv DEPENDS ${outputs})
    add_dependencies(${target} ${target}_spirv)
endfunction()

function(elm_compile_dxil source_dir file macros out_list)
    elm_dxc_executable(dxc)
    elm_spirv_profile(${file} profile)
    string(REGEX REPLACE "\\.hlsl$" "" output_name ${file})
    set(defines)
    foreach(macro IN LISTS macros)
        string(REPLACE "=" "_" suffix ${macro})
        string(APPEND output_name ".${suffix}")
        list(APPEND defines "-D${macro}")
    endforeach()

    set(output ${source_dir}/dxil/${output_name}.dxil)
    add_custom_command(
        OUTPUT  ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${source_dir}/dxil
        COMMAND ${dxc} -T ${profile} -E main ${ELM_DXC_COMPAT_DEFINES} ${defines} -Fo ${output} ${source_dir}/${file}
        DEPENDS ${source_dir}/${file} ${dxc}
        COMMENT "DXIL ${output_name}.dxil"
        VERBATIM
    )
    set(${out_list} ${${out_list}} ${output} PARENT_SCOPE)
endfunction()

function(elm_add_dxil_shaders target)
    cmake_parse_arguments(ARG "" "SOURCE_DIR" "VARIANTS" ${ARGN})
    if(NOT ELM_DXC_EXECUTABLE AND NOT ELM_DXC_FETCHED_EXECUTABLE)
        message(FATAL_ERROR "No DXC: the fetched release is missing and ELM_DXC_EXECUTABLE is not set")
    endif()

    file(GLOB sources RELATIVE ${ARG_SOURCE_DIR} CONFIGURE_DEPENDS ${ARG_SOURCE_DIR}/*.hlsl)
    set(outputs)
    foreach(file IN LISTS sources)
        elm_compile_dxil(${ARG_SOURCE_DIR} ${file} "" outputs)
    endforeach()
    foreach(variant IN LISTS ARG_VARIANTS)
        string(REPLACE ":" ";" parts ${variant})
        list(GET parts 0 file)
        list(GET parts 1 macros)
        string(REPLACE "," ";" macros ${macros})
        elm_compile_dxil(${ARG_SOURCE_DIR} ${file} "${macros}" outputs)
    endforeach()

    add_custom_target(${target}_dxil DEPENDS ${outputs})
    add_dependencies(${target} ${target}_dxil)
endfunction()
