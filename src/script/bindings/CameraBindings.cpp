#include "script/bindings/Bindings.hpp"

#include "script/values/Entity.hpp"

#include "graphics/Camera.hpp"
#include "graphics/SceneCamera.hpp"

#include "Scene/CameraComponent.hpp"
#include "Scene/Scene.hpp"

#include <cmath>
#include <numbers>

namespace elm::script::bindings {

    namespace {

        using math::Vector3;

        constexpr float RadiansToDegrees = 180.0f / std::numbers::pi_v<float>;

        /// What elm.camera reaches. Without a scene it is the view camera alone.
        struct CameraContext {
            Camera* camera { nullptr };
            Scene* scene { nullptr };
        };

        /// The view camera where the active camera node is: elm.camera reads it.
        Camera& ViewOf(lua_State* L)
        {
            const CameraContext& context = ContextOf<CameraContext>(L);
            Camera& view = RequireSystem(L, context.camera, "elm.camera");
            if (context.scene) {
                context.scene->UpdateWorldTransforms();
                (void)graphics::ViewThroughCamera(*context.scene, context.scene->ActiveCamera(), view);
            }
            return view;
        }

        /// Changes the view camera; the active camera node, if the scene has one, goes along.
        template <typename Change>
        void ChangeView(lua_State* L, Change change)
        {
            Camera& view = ViewOf(L);
            change(view);
            if (Scene* scene = ContextOf<CameraContext>(L).scene)
                (void)graphics::AlignCameraToView(*scene, scene->ActiveCamera(), view);
        }

        /// yaw and pitch that look along a direction (Camera: forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)).
        void LookAt(lua_State* L, const Vector3& target)
        {
            ChangeView(L, [&](Camera& camera) {
                const Vector3 direction = (target - camera.GetPosition()).Normalized();
                if (direction.Length() > 0.0f)
                    camera.SetOrientation(std::atan2(direction.x, direction.z), std::asin(direction.y));
            });
        }

        /// The field of view belongs to the active camera's component, if there is one.
        void SetFov(lua_State* L, float radians)
        {
            ViewOf(L).fovRadians = radians;
            Scene* scene = ContextOf<CameraContext>(L).scene;
            if (CameraComponent* camera = scene ? scene->Components<CameraComponent>().Find(scene->ActiveCamera()) : nullptr)
                camera->fovDegrees = radians * RadiansToDegrees;
        }

        std::optional<Entity> Active(lua_State* L)
        {
            const Scene& scene = RequireSystem(L, ContextOf<CameraContext>(L).scene, "elm.camera.active");
            const NodeId camera = scene.ActiveCamera();
            return camera ? std::optional<Entity>(Entity { camera }) : std::nullopt;
        }

        void SetActive(lua_State* L, const Entity& entity)
        {
            Scene& scene = RequireSystem(L, ContextOf<CameraContext>(L).scene, "elm.camera.set_active");
            if (!scene.SetActiveCamera(entity.id))
                luaL_error(L, "elm.camera.set_active: the entity is destroyed or has no Camera component");
        }

    } // namespace

    void RegisterCamera(lua::State& state, Camera* camera, Scene* scene)
    {
        state.SetContext(CameraContext { camera, scene });
        lua::Module(state, "elm.camera")
            .Function<&Active>("active")
            .Function<&SetActive>("set_active", { "entity" })
            .Function<[](lua_State* L) { return ViewOf(L).GetPosition(); }>("position")
            .Function<[](lua_State* L, const Vector3& position) { ChangeView(L, [&](Camera& view) { view.SetPosition(position); }); }>(
                "set_position", { "position" })
            .Function<[](lua_State* L, const Vector3& offset) { ChangeView(L, [&](Camera& view) { view.Translate(offset); }); }>(
                "translate", { "offset" })
            .Function<[](lua_State* L) { return ViewOf(L).GetForwardDirection(); }>("forward")
            .Function<[](lua_State* L) { return ViewOf(L).GetRightDirection(); }>("right")
            .Function<[](lua_State* L) { return ViewOf(L).GetYaw(); }>("yaw")
            .Function<[](lua_State* L) { return ViewOf(L).GetPitch(); }>("pitch")
            .Function<[](lua_State* L, float yaw, float pitch) { ChangeView(L, [&](Camera& view) { view.SetOrientation(yaw, pitch); }); }>(
                "set_orientation", { "yaw", "pitch" })
            .Function<[](lua_State* L, float yaw, float pitch) { ChangeView(L, [&](Camera& view) { view.Rotate(yaw, pitch); }); }>(
                "rotate", { "yaw", "pitch" })
            .Function<&LookAt>("look_at", { "target" })
            .Function<[](lua_State* L) { return ViewOf(L).fovRadians; }>("fov")
            .Function<&SetFov>("set_fov", { "radians" });
    }

} // namespace elm::script::bindings
