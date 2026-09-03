engine_add_library(Scene
    TestScenes.hpp
    TestScenes.cpp
    Transform.hpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
