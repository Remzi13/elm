#pragma once

#include "editor/ui/EditorServices.hpp"
#include "editor/ui/FileDialog.hpp"
#include "editor/ui/IImGuiWindow.hpp"

namespace elm {

    class ModelCatalog;
    class Scene;
    class SceneDocument;
    class SceneEdits;

    /// What Scene Hierarchy works with. Valid while the window lives: Editor destroys the project windows
    /// before the project closes.
    struct SceneHierarchyDeps {
        Scene& scene;
        ModelCatalog& models;
        /// Undo and redo by the keys.
        SceneEdits& edits;
        /// The status line shows its status, the window reports there what it does to the scene.
        SceneDocument& document;
        /// The node the window marks, written by it.
        Selection& selection;
        /// Ctrl+S.
        const SceneFileRequests& sceneFile;
    };

    /// The tree of the scene's nodes: search, selection, adding, renaming, moving and removing nodes. The components
    /// of the selected node are Inspector's, scene files the Scene menu's.
    /// Owner: ImGuiSystem (a project window), thread: update (UI).
    class SceneHierarchyWindow final : public IImGuiWindow {
    public:
        static constexpr const char* Name = "Scene Hierarchy";

        /// assets: where models from outside are imported to; models: where the dialog for them starts.
        SceneHierarchyWindow(Settings& settings, const SceneHierarchyDeps& deps, Path assets, Path models);

        [[nodiscard]] StringView GetName() const override { return Name; }
        void Render(render::FrameWriter& frame) override;

    private:
        struct TreeRow;
        /// A row on screen: one of the rows and how deep it lies in the tree.
        struct VisibleRow {
            size_t row { 0 };
            int depth { 0 };
        };

        /// The rows as they were before the tree is drawn: a row's menu or a drop changes the scene meanwhile.
        [[nodiscard]] Vector<TreeRow> ReadTreeRows() const;
        /// The rows under open rows that match the search, in the order of the tree.
        [[nodiscard]] Vector<VisibleRow> ListVisibleRows(const Vector<TreeRow>& rows) const;
        void AddVisibleRow(size_t rowIndex, int depth, const Vector<TreeRow>& rows, Vector<VisibleRow>& visible) const;

        [[nodiscard]] bool IsSearching() const { return m_filter[0] != '\0'; }
        void HandleKeys();
        void DrawToolbar();
        void DrawTree(const Vector<TreeRow>& rows);
        void DrawRow(const VisibleRow& visible, const Vector<TreeRow>& rows);
        void DrawRowName(const TreeRow& row);
        void DrawRenameField(const TreeRow& row);
        void AcceptNodeDrop(NodeId newParent);
        void DrawRowMenu(const TreeRow& row);
        void DrawEmptySpace();
        /// Adds under parent, invalid: a root.
        void DrawAddNodeItems(NodeId parent);
        void DrawStatusLine();
        void DrawModelDialog();
        /// A model file dropped from the Project window becomes a new root node.
        void AcceptDroppedModel();

        void Select(NodeId node);
        void StartRename(NodeId node);
        void DuplicateSelected();
        void RemoveSelected();

        SceneHierarchyDeps m_deps;
        Path m_assets;
        Path m_models;
        char m_filter[128] { "" };
        /// The nodes whose children show, by NodeId::Raw: a stale id opens nothing.
        Set<uint32_t> m_openRows;
        NodeId m_renaming;
        String m_renameText;
        /// The rename field took the focus: leaving it ends the rename.
        bool m_renameFocused { false };
        ui::FileDialog m_modelDialog;
        /// Where the node with the chosen model goes.
        NodeId m_modelDialogParent;
    };

} // namespace elm
