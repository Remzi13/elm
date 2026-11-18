#include "graphics/SceneCamera.hpp"

#include "graphics/Camera.hpp"

#include "Scene/CameraComponent.hpp"
#include "Scene/Scene.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace elm::graphics {

    namespace {

        constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

        [[nodiscard]] const CameraComponent* FindCamera(const Scene& scene, NodeId camera)
        {
            const ComponentStore<CameraComponent>* cameras = scene.FindComponents<CameraComponent>();
            return cameras && scene.IndexOf(camera) != Scene::NoNode ? cameras->Find(camera) : nullptr;
        }

        [[nodiscard]] math::Vector3 AxisOf(const math::Matrix4x4& transform, size_t column)
        {
            return { transform(0, column), transform(1, column), transform(2, column) };
        }

    } // namespace

    bool ViewThroughCamera(const Scene& scene, NodeId camera, Camera& view)
    {
        const CameraComponent* component = FindCamera(scene, camera);
        if (!component)
            return false;
        const math::Matrix4x4& world = scene.WorldTransforms()[scene.IndexOf(camera)];
        const math::Vector3 forward = AxisOf(world, 2).Normalized();
        view.SetPosition(AxisOf(world, 3));
        // The inverse of Camera::GetForwardDirection
        view.SetOrientation(std::atan2(forward.x, forward.z), std::asin(forward.y));
        view.fovRadians = component->fovDegrees * DegreesToRadians;
        view.zNear = component->nearPlane;
        view.zFar = component->farPlane;
        return true;
    }

    bool AlignCameraToView(Scene& scene, NodeId camera, const Camera& view)
    {
        if (!FindCamera(scene, camera))
            return false;
        scene.UpdateWorldTransforms();
        const math::Matrix4x4& current = scene.WorldTransforms()[scene.IndexOf(camera)];
        const math::Vector3 forward = view.GetForwardDirection();
        const math::Vector3 right = view.GetRightDirection();
        // As Camera::GetRayThrough: up completes the view's frame
        const std::array<math::Vector3, 3> axes { right, right.Cross(forward).Normalized(), forward };
        math::Matrix4x4 world = math::Matrix4x4::Identity();
        for (size_t column = 0; column < axes.size(); ++column) {
            const math::Vector3 axis = axes[column] * AxisOf(current, column).Length();
            world(0, column) = axis.x;
            world(1, column) = axis.y;
            world(2, column) = axis.z;
        }
        const math::Vector3 position = view.GetPosition();
        world(0, 3) = position.x;
        world(1, 3) = position.y;
        world(2, 3) = position.z;
        return scene.SetWorldTransform(camera, world);
    }

} // namespace elm::graphics
