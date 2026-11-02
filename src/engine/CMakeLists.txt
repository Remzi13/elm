engine_add_library(engine
    EngineSettings.hpp
    EngineSettings.cpp
    FrameClock.hpp
    FrameClock.cpp
    ProjectPlayStart.hpp
    ProjectPlayStart.cpp
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# What both programs share: the settings they read, the frame clock, how Play starts in a project.
# The programs (Game, Editor) own the systems themselves
target_link_libraries(engine PUBLIC core math input render platform Scene graphics physics project resource script)

# elm: the game alone, without the editor and ImGui; a built game is a copy of it
engine_add_executable(elm
    Game.hpp
    Game.cpp
    main.cpp
)
target_link_libraries(elm PRIVATE core platform project engine)
# The program's own code (Game, main), so the option reaches elm only
if(ELM_DEBUG_DRAW)
    target_compile_definitions(elm PRIVATE ELM_DEBUG_DRAW=1)
endif()
# In the build root next to editor, which runs and copies it (MSVC adds the config folder)
set_target_properties(elm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_command(TARGET elm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            ${CMAKE_BINARY_DIR}/shaders
)
