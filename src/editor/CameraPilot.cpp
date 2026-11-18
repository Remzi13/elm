#include "editor/CameraPilot.hpp"

#include "graphics/Camera.hpp"
#include "graphics/SceneCamera.hpp"

#include "Scene/Scene.hpp"

namespace elm {

    CameraPilot::CameraPilot(Scene& scene, Camera& view)
        : m_scene(scene)
        , m_view(view)
    {
    }

    void CameraPilot::Start(NodeId camera)
    {
        Stop();
        const float fovRadians = m_view.fovRadians;
        const float zNear = m_view.zNear;
        const float zFar = m_view.zFar;
        if (!graphics::ViewThroughCamera(m_scene, camera, m_view))
            return;
        m_camera = camera;
        m_followed = PoseOfView();
        m_fovRadians = fovRadians;
        m_zNear = zNear;
        m_zFar = zFar;
    }

    void CameraPilot::Stop()
    {
        if (!m_camera)
            return;
        m_camera = {};
        m_view.fovRadians = m_fovRadians;
        m_view.zNear = m_zNear;
        m_view.zFar = m_zFar;
    }

    void CameraPilot::FollowNode()
    {
        if (!m_camera)
            return;
        if (!graphics::ViewThroughCamera(m_scene, m_camera, m_view)) {
            Stop();
            return;
        }
        m_followed = PoseOfView();
    }

    void CameraPilot::FollowView()
    {
        if (!m_camera || PoseOfView() == m_followed)
            return;
        (void)graphics::AlignCameraToView(m_scene, m_camera, m_view);
        m_followed = PoseOfView();
    }

    auto CameraPilot::PoseOfView() const -> ViewPose
    {
        return { m_view.GetPosition(), m_view.GetYaw(), m_view.GetPitch() };
    }

} // namespace elm
