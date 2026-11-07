#pragma once

#include "editor/ui/EditorServices.hpp"
#include "editor/ui/FileDialog.hpp"
#include "editor/ui/IImGuiWindow.hpp"

#include "math/Matrix.hpp"

#include "script/EngineContext.hpp"

namespace elm {

    class ModelCatalog;
    class Scene;
    class SceneDocument;
    class SceneEdits;
    struct GizmoSettings;

    /// What Inspector works with. Valid while the window lives: Editor destroys the project windows before the
    /// project closes.
    struct NodeInspectorDeps {
        Scene& scene;
        ModelCatalog& models;
        /// Undo and redo by the keys.
        SceneEdits& edits;
        /// Failed edits go to its status.
        SceneDocument& document;
        /// The node the window shows.
        const Selection& selection;
        /// The gizmo mode the viewport reads, picked in the Transform section.
        GizmoSettings& gizmo;
        /// Whether the game runs: script instances are there in Play only.
        const script::PlayRequests& play;
        const ScriptInspection& scripts;
        /// Open in Editor of a model.
        const FileOpener& files;
        /// Ctrl+S.
        const SceneFileRequests& sceneFile;
    };

    /// The selected node: its name, parent, transform and components, written directly to the scene.
    /// Owner: ImGuiSystem (a project window), thread: update (UI).
    class NodeInspectorWindow final : public IImGuiWindow {
    public:
        /// The id after ### is its own: old imgui.ini files keep a hidden window named Inspector.
        static constexpr const char* Name = "Inspector###NodeInspector";

        /// The folders of the open project the window works with.
        struct Folders {
            /// Where models from outside are imported to.
            Path assets;
            Path models;
            Path scripts;
        };

        NodeInspectorWindow(Settings& settings, const NodeInspectorDeps& deps, Folders folders);

        [[nodiscard]] StringView GetName() const override { return Name; }
        void Render(render::FrameWriter& frame) override;
        /// Opens the window and brings it forward on the next frame (its tab in a dock too).
        void Show();

    private:
        [[nodiscard]] bool IsPlaying() const;
        void DrawNodeRows(NodeId node);
        void DrawParentCombo(NodeId node);

        void DrawTransformSection(NodeId node);
        void DrawTransformRows(NodeId node);
        void DrawGizmoRow();
        /// Writes a transform in the space the window edits.
        void WriteTransform(NodeId node, const math::Matrix4x4& transform);

        void DrawModelSection(NodeId node);
        void DrawModelRows(NodeId node);
        void OpenModelDialog(NodeId node);
        void DrawModelDialog();

        void DrawRigidBodySection(NodeId node);
        void DrawRigidBodyRows(NodeId node);

        void DrawColliderSection(NodeId node);
        void DrawColliderRows(NodeId node);

        /// The Script component: its module, the instance in Play mode, the props of the script with their
        /// defaults (ScriptInspection of the Scripts window); a changed prop becomes this node's own value.
        void DrawScriptSection(NodeId node);
        void DrawScriptRows(NodeId node);
        void DrawScriptModulePicker(ScriptComponent& script);
        void DrawScriptInstance(NodeId node);
        void DrawScriptProps(NodeId node);
        void DrawScriptProp(NodeId node, const String& name, const ScriptValue& fallback);
        /// Values the node keeps for props the script no longer has: shown to be removed.
        void DrawUnknownProps(ScriptComponent& script, const ScriptProps& props);
        void ResetScriptProps(NodeId node);

        void DrawAddComponent(NodeId node);
        void DrawAddModelMenu(NodeId node);
        void DrawAddScriptMenu(NodeId node);

        NodeInspectorDeps m_deps;
        Folders m_folders;
        bool m_editLocalTransform { true };
        bool m_focusRequested { false };
        char m_scriptFilter[128] { "" };
        Vector<String> m_scriptModules;
        ui::FileDialog m_modelDialog;
        /// The node that gets the chosen model.
        NodeId m_modelDialogNode;
    };

} // namespace elm
