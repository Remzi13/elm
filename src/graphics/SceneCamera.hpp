#pragma once

#include "Scene/NodeId.hpp"

namespace elm {
    class Camera;
    class Scene;
} // namespace elm

namespace elm::graphics {

    /// Puts the view camera where the camera node is (Scene::ActiveCamera in Play, the piloted one in the
    /// editor): it looks along the node's +Z axis with the CameraComponent's field of view and planes; the node's
    /// roll and scale are not used. False, and the view stays, for a node without a CameraComponent.
    /// Reads the world transforms as they are. Thread: update.
    [[nodiscard]] bool ViewThroughCamera(const Scene& scene, NodeId camera, Camera& view);

    /// The other way: turns and moves the camera node to where the view camera is, keeping the node's scale.
    /// Its CameraComponent stays as it is. False for a node without a CameraComponent. Thread: update.
    [[nodiscard]] bool AlignCameraToView(Scene& scene, NodeId camera, const Camera& view);

} // namespace elm::graphics
