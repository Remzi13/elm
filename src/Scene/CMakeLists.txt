engine_add_library(Scene
    NodeId.hpp
    NodeIdTable.hpp
    NodeIdTable.cpp
    ComponentTypes.hpp
    ComponentStores.hpp
    ComponentStores.cpp
    Scene.hpp
    Scene.cpp
    SceneAsset.hpp
    SceneAsset.cpp
    TestScenes.hpp
    TestScenes.cpp
    Transform.hpp
    ComponentStore.hpp
    ScriptComponent.hpp
    PhysicsComponents.hpp
    ModelComponent.hpp
    SceneSnapshot.hpp
    SceneSnapshot.cpp
)

target_link_libraries(Scene PUBLIC core math render)
