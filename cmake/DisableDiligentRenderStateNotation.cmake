if(NOT DEFINED DILIGENT_TOOLS_CMAKE)
    message(FATAL_ERROR "DILIGENT_TOOLS_CMAKE must point to DiligentTools/CMakeLists.txt")
endif()

file(READ "${DILIGENT_TOOLS_CMAKE}" diligent_tools_cmake)
if(NOT diligent_tools_cmake MATCHES "#######add_subdirectory\\(RenderStateNotation\\)")
    string(REPLACE "add_subdirectory(RenderStateNotation)"
                   "#######add_subdirectory(RenderStateNotation)"
                   diligent_tools_cmake "${diligent_tools_cmake}")
    file(WRITE "${DILIGENT_TOOLS_CMAKE}" "${diligent_tools_cmake}")
endif()
