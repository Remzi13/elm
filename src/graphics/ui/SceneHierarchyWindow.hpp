#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/FileDialog.hpp"
#include "graphics/ui/WindowMessages.hpp"

namespace elm {

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
    enum class GltfDialogAction {
        LoadModel,
        LoadScene,
        ExportModel,
        ExportScene
    };

    size_t m_selectedInstance { 0 };
    GizmoOperation m_gizmoOperation { GizmoOperation::Translate };
    bool m_useLocalSpace { false };
    char m_sceneFilePath[260] { "scene.scene" };
    ui::FileDialog m_gltfFileDialog;
    GltfDialogAction m_gltfDialogAction { GltfDialogAction::LoadModel };
    String m_sceneFileStatus;
    Vector<MessageBus::Subscription> m_querySubscriptions;
};

} // namespace elm