engine_add_library(platform
    PlatformSystem.hpp
    PlatformSystem.cpp
    Window.hpp
    Window.cpp
    Process.hpp
    Process.cpp
)

target_link_libraries(platform PUBLIC core math render input glfw)
