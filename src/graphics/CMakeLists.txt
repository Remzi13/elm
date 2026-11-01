engine_add_library(graphics
    Camera.hpp    
    Camera.cpp
    CameraController.hpp
    CameraController.cpp
    Model.cpp
    Model.hpp
    ModelHandler.hpp
    ModelManager.cpp
    ModelManager.hpp
    ModelImport.hpp
    ModelImport.cpp
    GameView.hpp
    GameView.cpp

    culling/SoftwareDepthBuffer.hpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCullingSystem.hpp
    culling/OcclusionCullingSystem.cpp
    debug/DebugDraw.hpp
    debug/DebugDraw.cpp
    debug/DebugDrawSystem.hpp
    debug/DebugDrawSystem.cpp
    debug/IDebugVisualizer.hpp
    debug/ColliderVisualizer.hpp
    debug/ColliderVisualizer.cpp
    debug/PhysicsVisualizer.hpp
    debug/PhysicsVisualizer.cpp
    debug/WorldAxesVisualizer.hpp
    debug/WorldAxesVisualizer.cpp
    gizmo/GizmoSettings.hpp
    gizmo/TransformGizmo.hpp
    gizmo/TransformGizmo.cpp
)

target_link_libraries(graphics PUBLIC core render platform resource)
