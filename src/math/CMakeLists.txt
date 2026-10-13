engine_add_library(math
    Vector.hpp
    Vector.cpp
    Matrix.hpp
    Matrix.cpp
    Primitivs.hpp
    Rotation.hpp
    Rotation.cpp
)

target_include_directories(math PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
