#include "script/bindings/Bindings.hpp"

#include "graphics/Camera.hpp"

#include <cmath>

namespace elm::script::bindings {

    namespace {

        using math::Vector3;

        /// What elm.camera reaches.
        struct CameraContext {
            Camera* camera { nullptr };
        };

        Camera& CameraOf(lua_State* L) { return RequireSystem(L, ContextOf<CameraContext>(L).camera, "elm.camera"); }

        /// yaw and pitch that look along a direction (Camera: forward = (sin yaw cos pitch, sin pitch, cos yaw cos pitch)).
        void LookAt(lua_State* L, const Vector3& target)
        {
            auto& camera = CameraOf(L);
            const Vector3 direction = (target - camera.GetPosition()).Normalized();
            if (direction.Length() == 0.0f)
                return;
            camera.SetOrientation(std::atan2(direction.x, direction.z), std::asin(direction.y));
        }

    } // namespace

    void RegisterCamera(lua::State& state, Camera* camera)
    {
        state.SetContext(CameraContext { camera });
        lua::Module(state, "elm.camera")
            .Function<[](lua_State* L) { return CameraOf(L).GetPosition(); }>("position")
            .Function<[](lua_State* L, const Vector3& position) { CameraOf(L).SetPosition(position); }>("set_position", { "position" })
            .Function<[](lua_State* L, const Vector3& offset) { CameraOf(L).Translate(offset); }>("translate", { "offset" })
            .Function<[](lua_State* L) { return CameraOf(L).GetForwardDirection(); }>("forward")
            .Function<[](lua_State* L) { return CameraOf(L).GetRightDirection(); }>("right")
            .Function<[](lua_State* L) { return CameraOf(L).GetYaw(); }>("yaw")
            .Function<[](lua_State* L) { return CameraOf(L).GetPitch(); }>("pitch")
            .Function<[](lua_State* L, float yaw, float pitch) { CameraOf(L).SetOrientation(yaw, pitch); }>("set_orientation", { "yaw", "pitch" })
            .Function<[](lua_State* L, float yaw, float pitch) { CameraOf(L).Rotate(yaw, pitch); }>("rotate", { "yaw", "pitch" })
            .Function<&LookAt>("look_at", { "target" })
            .Function<[](lua_State* L) { return CameraOf(L).fovRadians; }>("fov")
            .Function<[](lua_State* L, float radians) { CameraOf(L).fovRadians = radians; }>("set_fov", { "radians" });
    }

} // namespace elm::script::bindings
