#pragma once

#include "math/Vector.hpp"

#include "Scene/NodeId.hpp"

namespace elm {

    class Camera;
    class Scene;

    /// Piloting a camera node in Edit (docs/editor/Редактор.md): the view looks through the node, with its field of
    /// view and planes, and the free camera of the editor moves the node with the view. Stop gives the view its own
    /// lens back. A node that is gone or has lost its CameraComponent ends piloting on the next FollowNode.
    /// Owner: Editor, which ends it on Play and when the selection moves to another node; the Inspector and the
    /// viewport start and stop it. Thread: update.
    class CameraPilot {
    public:
        CameraPilot(Scene& scene, Camera& view);

        CameraPilot(const CameraPilot&) = delete;
        CameraPilot& operator=(const CameraPilot&) = delete;
        CameraPilot(CameraPilot&&) = delete;
        CameraPilot& operator=(CameraPilot&&) = delete;

        /// Ends piloting another node; nothing happens for a node without a CameraComponent.
        void Start(NodeId camera);
        void Stop();
        /// The piloted node, an invalid id when there is none.
        [[nodiscard]] NodeId Piloted() const noexcept { return m_camera; }

        /// Before anything moves the view: the view takes the node's place, so edits of the node show.
        void FollowNode();
        /// After the free camera and the windows: the node takes the view's place if they moved the view since
        /// FollowNode. Called in the UI frame, so the editor's history records the move as an edit of the node.
        void FollowView();

    private:
        /// Where the view looks from: what FollowView compares.
        struct ViewPose {
            math::Vector3 position;
            float yaw { 0.0f };
            float pitch { 0.0f };

            bool operator==(const ViewPose&) const = default;
        };

        [[nodiscard]] ViewPose PoseOfView() const;

        Scene& m_scene;
        Camera& m_view;
        NodeId m_camera;
        /// The view as FollowNode left it.
        ViewPose m_followed;
        /// The view's own lens, back on Stop.
        float m_fovRadians { 0.0f };
        float m_zNear { 0.0f };
        float m_zFar { 0.0f };
    };

} // namespace elm
