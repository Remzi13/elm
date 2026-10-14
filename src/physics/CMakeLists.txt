engine_add_library(physics
    PhysicsSystem.hpp
    PhysicsSystem.cpp
)

target_link_libraries(physics PUBLIC core math Scene)
# Private: Jolt puts its compile definitions (_HAS_EXCEPTIONS=0 among them) on everyone linking it publicly,
# and the engine needs C++ exceptions (Lua errors are exceptions). Only PhysicsSystem.cpp sees Jolt headers.
target_link_libraries(physics PRIVATE Jolt)

target_include_directories(physics PRIVATE
    ${JoltPhysics_SOURCE_DIR}/Jolt
)
