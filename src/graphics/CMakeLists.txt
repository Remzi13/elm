engine_add_library(graphics
    Camera.hpp    
    Camera.cpp
    CameraController.hpp
    CameraController.cpp
    ModelMeshes.hpp
    ModelMeshes.cpp
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

target_link_libraries(graphics PUBLIC core render platform model resource)
