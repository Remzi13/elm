engine_add_library(Scene
    TestScenes.hpp
    TestScenes.cpp
    Transform.hpp
    ComponentStore.hpp
    ScriptComponent.hpp
    PhysicsComponents.hpp
    SceneSnapshot.hpp
    SceneSnapshot.cpp
)

target_link_libraries(Scene PUBLIC core math render)
