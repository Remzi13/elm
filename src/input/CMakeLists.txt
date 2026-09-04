engine_add_library(input
    InputTypes.hpp
    InputSubscriber.hpp
    InputAction.hpp
    Input.hpp
    Input.cpp    
)

target_include_directories(input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(input PUBLIC glfw)
