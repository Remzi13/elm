#pragma once

#include "editor/ui/IImGuiWindow.hpp"
#include "editor/ui/FileDialog.hpp"

#include "graphics/ModelImport.hpp"
#include "model/ModelCatalog.hpp"
#include "graphics/ModelMeshes.hpp"
#include "render/ViewPort.hpp"

#include <functional>
#include <optional>

namespace elm {

class Camera;
class Scene;

/// What the Model Editor works with. Valid while the window lives: Editor destroys the project windows
/// before the project closes.
struct ModelEditorDeps {
    /// For the preview's meshes and view.
    render::RenderResources& resources;
    Scene& scene;
    ModelCatalog& models;
    /// Add to Scene puts the model in front of it.
    const Camera& camera;
};

/// Model editor: creates, imports (glTF), opens and saves engine models (.model) and edits models
/// of the current scene. Every edit works on a CPU copy of the model with undo/redo; the scene
/// sees the result only on "Apply to Scene", "Add to Scene" or a save over the file of a scene model.
class ModelEditorWindow final : public IImGuiWindow {
public:
    /// The project's assets folder (imports are copied into it) and its models folder (where dialogs start).
    ModelEditorWindow(Settings& settings, const ModelEditorDeps& deps, Path assetsFolder, Path modelsFolder);

    [[nodiscard]] StringView GetName() const override { return "Model Editor"; }
    void Render(render::FrameWriter& frame) override;
    /// Opens a .model file in the next Render (FileOpener::model).
    void OpenModelFile(const Path& file);
    /// Opens a model of the scene ModelCatalog in the next Render (FileOpener::sceneModel).
    void OpenSceneModel(ModelHandler handler);

private:
    enum class DialogAction {
        Open,
        Import,
        AppendGltf,
        AppendModel,
        SaveAs,
        SaveImported
    };

    /// A glTF scene being split into models before any of them becomes the edited document.
    struct ImportSession {
        Path path;
        SceneAsset asset;
        /// One flag per scene node of the asset.
        Vector<bool> selected;
        graphics::SceneImportOptions options;
        Vector<graphics::ImportedModel> results;
        /// The files "Save Models" wrote, one per result, for the models added to the scene afterwards.
        Vector<Path> savedFiles;
        size_t selectedResult { 0 };
        bool resultsDirty { true };
    };

    using Action = std::function<void()>;

    // Documents
    void SetDocument(Model model, Path filePath, ModelHandler sceneModel, String status);
    void CloseDocument();
    void OpenFile(const Path& path);
    /// Single-mesh files open right away, scenes start an import session.
    void ImportGltf(const Path& file);
    void OpenImportedResult();
    void SaveImported(const Path& anyFileInDirectory);
    void AddImportedToScene();
    void OpenFromScene(ModelHandler handler);
    void Append(const Path& path, bool gltf);
    bool SaveTo(const Path& path);
    void Save();
    void ApplyToScene();
    void AddToScene();
    /// Runs the action (deferred to window level), after the user agreed to drop unsaved changes if any.
    void RunDiscarding(Action action);

    // Edit history
    /// Call before changing the model. Continuous edits with the same key while the user keeps
    /// interacting with one widget (dragging, typing) produce a single undo step.
    void RecordUndo(StringView key, bool continuous = false);
    void Undo();
    void Redo();

    // Geometry operations, each one an undo step
    void TransformGeometry(const Matrix4x4& transform, bool selectedOnly, StringView description);
    void MovePivot(bool bottom);
    void FlipFaces(bool selectedOnly);
    void MergeByColor();
    void DuplicateSelected();
    void DeleteSelected();
    void MoveSelected(int direction);

    // UI
    void DrawMenuBar();
    void DrawShortcuts();
    void DrawEmptyState();
    void DrawProperties();
    void DrawSubmeshes();
    void DrawTransformTools();
    void DrawPreview(render::FrameWriter& frame);
    void DrawImport();
    void DrawImportTree();
    void DrawDiscardPopup();
    void HandleFileDialog();
    void OpenFileDialog(DialogAction action);

    /// Uploads the previewed model (document or selected import result) when it changed.
    void UpdatePreview();
    /// Model shown in the preview: the selected import result or the document.
    [[nodiscard]] const Model* GetPreviewSource() const;
    /// The file of the scene model the document was applied to: the document's file unless it has unsaved edits.
    void RecordSceneFile() const;
    /// A file was written: its model in the catalog holds what the file holds now, every node with it shows that.
    void UpdateModelOfFile(const Path& file, const Model& model) const;
    [[nodiscard]] bool IsSceneModelAlive() const;

    ModelEditorDeps m_deps;
    Path m_assetsFolder;
    Path m_modelsFolder;
    // The preview model lives in its own catalog, apart from the scene models; its meshes once rendering is up
    ModelCatalog m_previewModels;
    std::optional<graphics::ModelMeshes> m_previewMeshes;
    ModelHandler m_previewModel;
    render::ViewPort m_viewPort;
    bool m_previewDirty { false };

    std::optional<ImportSession> m_import;

    // Document
    std::optional<Model> m_model;
    Path m_filePath;
    /// Scene model the document was opened from; Apply to Scene replaces it.
    ModelHandler m_sceneModel;
    bool m_dirty { false };
    size_t m_selected { 0 };

    // History
    Vector<Model> m_undo;
    Vector<Model> m_redo;
    String m_lastEditKey;
    bool m_editInProgress { false };

    // Tools
    Vector3 m_toolTranslation { 0.0f, 0.0f, 0.0f };
    Vector3 m_toolRotation { 0.0f, 0.0f, 0.0f };
    Vector3 m_toolScale { 1.0f, 1.0f, 1.0f };
    bool m_toolSelectedOnly { false };

    // Preview
    bool m_highlightSelected { true };
    bool m_soloSelected { false };
    float m_cameraYaw { 0.6f };
    float m_cameraPitch { 0.4f };
    float m_cameraZoom { 1.0f };

    ui::FileDialog m_fileDialog;
    DialogAction m_dialogAction { DialogAction::Open };
    /// Waits for the answer of the unsaved changes popup.
    Action m_pendingAction;
    /// Runs at window level on the next opportunity (see Render).
    Action m_deferredAction;
    bool m_openDiscardPopup { false };
    std::optional<ModelHandler> m_requestedSceneModel;
    /// Asked for through OpenModelFile, opened in the next Render (after the unsaved-changes question).
    std::optional<Path> m_requestedFile;
    bool m_focusRequested { false };
    String m_status;
};

} // namespace elm
