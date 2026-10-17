#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/FileDialog.hpp"
#include "graphics/ui/WindowMessages.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

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
    void DrawToolbar(ImGuiWindowContext& context);
    void DrawHierarchyTree(ImGuiWindowContext& context);
    void DrawNodeItem(ImGuiWindowContext& context, size_t nodeIndex, const Vector<Vector<size_t>>& nodeChildren);
    void DrawNodeDetails(ImGuiWindowContext& context, NodeId nodeId);
    void DrawTransformSection(Scene& scene, NodeId nodeId);
    void DrawModelComponent(Scene& scene, NodeId nodeId, graphics::ModelManager& models);
    void DrawRigidBodyComponent(Scene& scene, NodeId nodeId);
    void DrawColliderComponent(Scene& scene, NodeId nodeId, const graphics::ModelManager& models);
    void DrawScriptComponent(Scene& scene, NodeId nodeId);
    void DrawAddComponentPopup(Scene& scene, NodeId nodeId, const graphics::ModelManager& models);

    NodeId DuplicateNode(Scene& scene, NodeId sourceId);
    NodeId AddPrimitiveNode(Scene& scene, graphics::ModelManager& models, NodeId parentId, const char* name, int primitiveType);
    void RefreshScriptModules();

    NodeId m_selectedNode;
    GizmoOperation m_gizmoOperation { GizmoOperation::Translate };
    bool m_useLocalSpace { false };
    bool m_editLocalTransform { true };
    float m_hierarchyWidth { 280.0f };

    char m_sceneFilePath[260] { "scene.scene" };
    char m_hierarchyFilter[128] { "" };
    char m_scriptFilter[128] { "" };
    char m_newPropName[64] { "newProp" };
    int m_newPropType { 0 };

    ui::FileDialog m_gltfFileDialog;
    ui::FileDialog m_sceneOpenFileDialog;
    ui::FileDialog m_sceneSaveFileDialog;

    String m_sceneFileStatus;
    Vector<MessageBus::Subscription> m_querySubscriptions;
    Vector<String> m_scriptModules;
    bool m_scriptModulesLoaded { false };
};

} // namespace elm