engine_add_library(physics
    PhysicsSystem.cpp
)

target_link_libraries(physics PUBLIC Jolt)

target_include_directories(physics PUBLIC
    ${JoltPhysics_SOURCE_DIR}/Jolt
)