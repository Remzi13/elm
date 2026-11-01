#pragma once

#include "editor/ui/IImGuiWindow.hpp"
#include "editor/ui/FileDialog.hpp"
#include "editor/ui/EditorServices.hpp"
#include "graphics/gizmo/GizmoSettings.hpp"
#include "script/EngineContext.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/ModelComponent.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

namespace elm {

class ModelCatalog;
class Scene;

/// What Scene Hierarchy works with. Valid while the window lives: Editor destroys the project windows
/// before the project closes.
struct SceneHierarchyDeps {
    Scene& scene;
    ModelCatalog& models;
    /// The node the window shows, written by it.
    Selection& selection;
    /// The gizmo mode the window picks for the viewport.
    GizmoSettings& gizmo;
    /// Whether the game runs: saving and permanent edits wait for Stop.
    const script::PlayRequests& play;
    const ScriptInspection& scripts;
    /// Open in Editor of a model.
    const FileOpener& files;
};

/// The scene and the components of its nodes: the tree, the details of the selected node, scene files.
/// Owner: ImGuiSystem (a project window), thread: update (UI).
class SceneHierarchyWindow final : public IImGuiWindow {
public:
    using GizmoOperation = elm::GizmoOperation;
    using GizmoSpace = elm::GizmoSpace;

    /// The folders of the open project the window works with.
    struct Folders {
        /// What scene files reference models relative to.
        Path assets;
        Path scenes;
        Path models;
        Path scripts;
    };

    /// openedScene: the scene file the editor opened with the project (its startup scene), shown in the
    /// Scene File field so that Save writes it; empty: a name no scene has yet.
    SceneHierarchyWindow(Settings& settings, const SceneHierarchyDeps& deps, Folders folders, const Path& openedScene);

    [[nodiscard]] StringView GetName() const override { return "Scene Hierarchy"; }
    void Render(render::FrameWriter& frame) override;
    /// Replaces the scene with a scene file in the next Render (FileOpener::scene).
    void OpenScene(const Path& file);

private:
    [[nodiscard]] bool IsPlaying() const;
    void DrawToolbar();
    void DrawHierarchyTree();
    void DrawNodeItem(size_t nodeIndex, const Vector<Vector<size_t>>& nodeChildren);
    void DrawNodeDetails(NodeId nodeId);
    void DrawTransformSection(Scene& scene, NodeId nodeId);
    void DrawModelComponent(Scene& scene, NodeId nodeId, ModelCatalog& models);
    void DrawRigidBodyComponent(Scene& scene, NodeId nodeId);
    void DrawColliderComponent(Scene& scene, NodeId nodeId, const ModelCatalog& models);
    /// The Script component: its module, the instance in Play mode, the props of the script with their
    /// defaults (ScriptInspection of the Scripts window); a changed prop becomes this node's own value.
    void DrawScriptComponent(Scene& scene, NodeId nodeId);
    void DrawScriptModule(ScriptComponent& script);
    void DrawScriptInstance(NodeId nodeId, const ScriptInspection& scripts);
    void DrawScriptProps(Scene& scene, NodeId nodeId, const ScriptInspection& scripts);
    /// Values the node keeps for props the script no longer has: shown to be removed.
    void DrawUnknownProps(ScriptComponent& script, const ScriptProps& props);
    void DrawAddComponentPopup(Scene& scene, NodeId nodeId, const ModelCatalog& models);

    NodeId DuplicateNode(Scene& scene, NodeId sourceId);
    NodeId AddPrimitiveNode(Scene& scene, ModelCatalog& models, NodeId parentId, const char* name, int primitiveType);
    void RefreshScriptModules();
    /// Replaces the scene with a scene file; the file field shows it from then on.
    void LoadSceneFile(const Path& file);
    /// The Scene File field names a scene relative to the scenes folder, one outside of it in full.
    void ShowSceneFile(const Path& file);
    /// A model file dropped from the Project window becomes a new root node.
    void AcceptDroppedModel();
    /// new.scene, new_1.scene, ...: the first name in the scenes folder no file has.
    [[nodiscard]] String UnusedSceneFileName() const;
    /// The new models into the project's models folder, then the scene that references them.
    [[nodiscard]] EngineResult<void> SaveScene(const Path& path);

    SceneHierarchyDeps m_deps;
    Folders m_folders;
    bool m_editLocalTransform { true };
    float m_hierarchyWidth { 280.0f };

    /// The scene Save writes, relative to the scenes folder.
    char m_sceneFilePath[260] { };
    char m_hierarchyFilter[128] { "" };
    char m_scriptFilter[128] { "" };

    enum class ModelDialogAction {
        AssignToComponent,
        AddNewRootNode,
        AddNewChildNode
    };

    ui::FileDialog m_gltfFileDialog;
    ui::FileDialog m_sceneOpenFileDialog;
    ui::FileDialog m_sceneSaveFileDialog;
    ui::FileDialog m_modelOpenFileDialog;
    ui::FileDialog m_gltfSceneImportFileDialog;
    ModelDialogAction m_modelDialogAction { ModelDialogAction::AssignToComponent };
    NodeId m_modelDialogTargetNode;
    bool m_deferModelOpen { false };
    bool m_deferGltfSceneImport { false };
    /// Asked for through OpenScene, loaded in the next Render: not in the middle of another window's UI.
    std::optional<Path> m_requestedScene;

    String m_sceneFileStatus;
    Vector<String> m_scriptModules;
    bool m_scriptModulesLoaded { false };
};

} // namespace elm