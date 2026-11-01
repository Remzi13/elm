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
    GraphicsSystem.hpp
    GraphicsSystem.cpp
    GameView.cpp

    culling/SoftwareDepthBuffer.hpp
    culling/SoftwareDepthBuffer.cpp
    culling/OcclusionCulling.hpp
    culling/OcclusionCulling.cpp
    debug/DebugDraw.hpp
    debug/DebugDraw.cpp
    debug/DebugDrawing.hpp
    debug/DebugDrawing.cpp
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

target_link_libraries(graphics PUBLIC core render platform model Scene resource)
