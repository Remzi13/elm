engine_add_library(platform
    Window.hpp
    Window.cpp
)

target_link_libraries(platform PUBLIC core math render glfw)
