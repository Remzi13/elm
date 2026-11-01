engine_add_library(engine
    IEngineLayer.hpp
    Engine.hpp
    Engine.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# The engine: the systems of a running game, the project and the frame loop
target_link_libraries(engine PUBLIC core math input render platform Scene graphics physics project resmgr script)

# elm: the game alone, without the editor and ImGui; a built game is a copy of it
engine_add_executable(elm
    main.cpp
    PlayerLayer.hpp
    PlayerLayer.cpp
)
target_link_libraries(elm PRIVATE core platform project engine)
# PlayerLayer is the program's own, so the option reaches elm only
if(ELM_DEBUG_DRAW)
    target_compile_definitions(elm PRIVATE ELM_DEBUG_DRAW=1)
endif()
# In the build root next to elm_editor, which runs and copies it (MSVC adds the config folder)
set_target_properties(elm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_command(TARGET elm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            ${CMAKE_BINARY_DIR}/shaders
)
