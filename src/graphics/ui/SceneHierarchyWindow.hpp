#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/FileDialog.hpp"
#include "graphics/ui/WindowMessages.hpp"

namespace elm {

struct ModelComponent;

class SceneHierarchyWindow final : public IImGuiWindow {
public:
    using GizmoOperation = elm::GizmoOperation;
    using GizmoSpace = elm::GizmoSpace;

    using IImGuiWindow::IImGuiWindow;

    [[nodiscard]] StringView GetName() const override { return "Scene Hierarchy"; }
    void Render(ImGuiWindowContext& context) override;

protected:
    void OnAttach() override;

private:
    /// Visible, color and model of the selected node; the editor writes the component in place.
    void DrawModel(ModelComponent& component, const graphics::ModelManager& models);

    NodeId m_selectedNode;
    GizmoOperation m_gizmoOperation { GizmoOperation::Translate };
    bool m_useLocalSpace { false };
    char m_sceneFilePath[260] { "scene.scene" };
    ui::FileDialog m_gltfFileDialog;
    String m_sceneFileStatus;
    Vector<MessageBus::Subscription> m_querySubscriptions;
};

} // namespace elm