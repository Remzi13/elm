engine_add_library(core
    EngineApp.cpp
)

target_link_libraries(core PUBLIC
    graphics
    physics
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
