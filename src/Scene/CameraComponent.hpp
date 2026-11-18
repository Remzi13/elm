#pragma once

#include "Scene/ComponentTypes.hpp"

namespace elm {

    /// A camera on a scene node: in Play the view looks through the scene's active camera (Scene::ActiveCamera,
    /// graphics::ViewThroughCamera), along the node's +Z axis, with the node's Y axis kept up only as far as
    /// the view camera can (it has no roll).
    struct CameraComponent {
        static constexpr ComponentType Type = ComponentType::Camera;

        float fovDegrees { 60.0f };
        float nearPlane { 0.2f };
        float farPlane { 500.0f };

        bool operator==(const CameraComponent&) const = default;
    };

} // namespace elm
