#pragma once

#include "graphics/ui/IImGuiWindow.hpp"
#include "graphics/ui/FileDialog.hpp"

#include "graphics/ModelImport.hpp"
#include "graphics/ModelManager.hpp"
#include "render/ViewPort.hpp"

#include <functional>
#include <optional>

namespace elm {

/// Model editor: creates, imports (glTF), opens and saves engine models (.model) and edits models
/// of the current scene. Every edit works on a CPU copy of the model with undo/redo; the scene
/// sees the result only on "Apply to Scene" or "Add to Scene".
class ModelEditorWindow final : public IImGuiWindow {
public:
    explicit ModelEditorWindow(Settings& settings)
        : IImGuiWindow(settings)
    {
        m_visible = false;
    }

    [[nodiscard]] StringView GetName() const override { return "Model Editor"; }
    void Render(ImGuiWindowContext& context) override;

protected:
    void OnAttach() override;

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
        size_t selectedResult { 0 };
        bool resultsDirty { true };
    };

    using Action = std::function<void(ImGuiWindowContext&)>;

    // Documents
    void SetDocument(graphics::Model model, Path filePath, graphics::ModelHandler sceneModel, String status);
    void CloseDocument();
    void OpenFile(const Path& path);
    /// Single-mesh files open right away, scenes start an import session.
    void ImportGltf(const Path& path);
    void OpenImportedResult();
    void SaveImported(const Path& anyFileInDirectory);
    void AddImportedToScene(ImGuiWindowContext& context);
    void OpenFromScene(ImGuiWindowContext& context, graphics::ModelHandler handler);
    void Append(const Path& path, bool gltf);
    bool SaveTo(const Path& path);
    void Save();
    void ApplyToScene(ImGuiWindowContext& context);
    void AddToScene(ImGuiWindowContext& context);
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
    void DrawMenuBar(ImGuiWindowContext& context);
    void DrawShortcuts(ImGuiWindowContext& context);
    void DrawEmptyState();
    void DrawProperties(ImGuiWindowContext& context);
    void DrawSubmeshes();
    void DrawTransformTools();
    void DrawPreview(ImGuiWindowContext& context);
    void DrawImport(ImGuiWindowContext& context);
    void DrawImportTree();
    void DrawDiscardPopup(ImGuiWindowContext& context);
    void HandleFileDialog(ImGuiWindowContext& context);
    void OpenFileDialog(DialogAction action);

    /// Uploads the previewed model (document or selected import result) when it changed.
    void UpdatePreview();
    /// Model shown in the preview: the selected import result or the document.
    [[nodiscard]] const graphics::Model* GetPreviewSource() const;
    [[nodiscard]] bool IsSceneModelAlive(const ImGuiWindowContext& context) const;

    // Preview resources live in their own manager, apart from the scene models
    std::optional<graphics::ModelManager> m_previewModels;
    graphics::ModelHandler m_previewModel;
    render::ViewPort m_viewPort;
    bool m_previewDirty { false };

    std::optional<ImportSession> m_import;

    // Document
    std::optional<graphics::Model> m_model;
    Path m_filePath;
    /// Scene model the document was opened from; Apply to Scene replaces it.
    graphics::ModelHandler m_sceneModel;
    bool m_dirty { false };
    size_t m_selected { 0 };

    // History
    Vector<graphics::Model> m_undo;
    Vector<graphics::Model> m_redo;
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
    std::optional<graphics::ModelHandler> m_requestedSceneModel;
    bool m_focusRequested { false };
    String m_status;
    Vector<MessageBus::Subscription> m_querySubscriptions;
};

} // namespace elm
