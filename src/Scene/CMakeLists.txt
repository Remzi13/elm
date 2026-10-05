engine_add_library(Scene
    TestScenes.hpp
    TestScenes.cpp
    Transform.hpp
)

target_link_libraries(Scene PUBLIC core math render)
