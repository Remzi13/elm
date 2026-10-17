#include "graphics/ui/ModelEditorWindow.hpp"

#include "core/AssetPaths.hpp"
#include "graphics/ui/WindowMessages.hpp"
#include "render/RenderSystem.hpp"
#include "render/api/FrameWriter.hpp"
#include "resmgr/GltfExporter.hpp"
#include "resmgr/Serializer.hpp"
#include "Scene/ModelComponent.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

namespace elm {
namespace {

    constexpr size_t MaxUndoSteps = 100;
    constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

    ImTextureID ToTextureId(render::TextureHandle texture)
    {
        if (!texture.IsValid())
            return nullptr;
        // Real texture views are aligned pointers, so the lowest bit marks an engine handle
        const auto value = (static_cast<uintptr_t>(texture.Raw()) << 1) | 1;
        return reinterpret_cast<ImTextureID>(value);
    }

    int ResizeInputBuffer(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
            return 0;

        auto& buffer = *static_cast<Vector<char>*>(data->UserData);
        buffer.resize(static_cast<size_t>(data->BufTextLen) + 1);
        data->Buf = buffer.data();
        return 0;
    }

    /// Edits a copy of value; returns the new text when it changed.
    std::optional<String> InputString(const char* label, const String& value)
    {
        Vector<char> buffer(value.begin(), value.end());
        buffer.push_back('\0');
        if (!ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_CallbackResize,
                ResizeInputBuffer, &buffer))
            return std::nullopt;
        return String(buffer.data(), std::char_traits<char>::length(buffer.data()));
    }

    [[nodiscard]] String ToString(size_t value)
    {
        return String(std::to_string(value));
    }

    [[nodiscard]] bool IsEmpty(const AABB& bounds) noexcept
    {
        return bounds.minBounds.x > bounds.maxBounds.x;
    }

    void Merge(AABB& bounds, const AABB& other) noexcept
    {
        bounds.minBounds.x = (std::min)(bounds.minBounds.x, other.minBounds.x);
        bounds.minBounds.y = (std::min)(bounds.minBounds.y, other.minBounds.y);
        bounds.minBounds.z = (std::min)(bounds.minBounds.z, other.minBounds.z);
        bounds.maxBounds.x = (std::max)(bounds.maxBounds.x, other.maxBounds.x);
        bounds.maxBounds.y = (std::max)(bounds.maxBounds.y, other.maxBounds.y);
        bounds.maxBounds.z = (std::max)(bounds.maxBounds.z, other.maxBounds.z);
    }

    [[nodiscard]] String MakeFileName(const String& modelName)
    {
        String fileName;
        for (const char c : modelName) {
            const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            fileName += allowed ? c : '_';
        }
        return (fileName.empty() ? String("model") : fileName) + ".model";
    }

    [[nodiscard]] Matrix4x4 RotationZ(float radians) noexcept
    {
        Matrix4x4 result = Matrix4x4::Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result(0, 0) = c; result(0, 1) = -s;
        result(1, 0) = s; result(1, 1) = c;
        return result;
    }

    /// Translation * Yaw * Pitch * Roll * Scale, angles in degrees.
    [[nodiscard]] Matrix4x4 MakeTransform(const Vector3& translation, const Vector3& rotation, const Vector3& scale) noexcept
    {
        return Matrix4x4::Translation(translation)
            * Matrix4x4::RotationY(rotation.y * DegreesToRadians)
            * Matrix4x4::RotationX(rotation.x * DegreesToRadians)
            * RotationZ(rotation.z * DegreesToRadians)
            * Matrix4x4::Scaling(scale);
    }

    [[nodiscard]] MeshData FlipMeshFaces(const MeshData& source)
    {
        MeshData result = source;
        for (auto& vertex : result.vertices)
            vertex.normal = vertex.normal * -1.0f;
        for (size_t index = 0; index + 2 < result.indices.size(); index += 3)
            std::swap(result.indices[index + 1], result.indices[index + 2]);
        return result;
    }

    void AppendMeshData(MeshData& target, const MeshData& source)
    {
        const auto offset = static_cast<uint32_t>(target.vertices.size());
        target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
        target.indices.reserve(target.indices.size() + source.indices.size());
        for (const auto index : source.indices)
            target.indices.push_back(index + offset);
        Merge(target.localBounds, source.localBounds);
    }

    [[nodiscard]] size_t CountTriangles(const graphics::Model& model) noexcept
    {
        size_t triangles = 0;
        for (const auto& submesh : model.submeshes)
            triangles += submesh.data ? submesh.data->indices.size() / 3 : 0;
        return triangles;
    }

    /// The Model component an imported glTF node gets in the scene: the model it went into, with the color
    /// and the visible flag the node had.
    [[nodiscard]] ModelComponent ImportedNodeModel(const SceneAsset& asset, size_t nodeIndex, graphics::ModelHandler model)
    {
        const auto* sourceModels = asset.components.FindComponents<ModelComponent>();
        const ModelComponent* source = sourceModels ? sourceModels->Find(SceneAsset::NodeRef(nodeIndex)) : nullptr;
        ModelComponent component = source ? *source : ModelComponent { };
        component.model = model;
        return component;
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Window
// ─────────────────────────────────────────────────────────────────────────────

void ModelEditorWindow::OnAttach()
{
    m_querySubscriptions.push_back(RegisterQuery<OpenModelEditor>([this] {
        return OpenModelEditor { [this](graphics::ModelHandler handler) {
            if (handler)
                m_requestedSceneModel = handler;
            m_visible = true;
            m_focusRequested = true;
        } };
    }));
}

void ModelEditorWindow::Render(ImGuiWindowContext& context)
{
    if (!m_visible)
        return;

    if (!m_previewModels)
        m_previewModels.emplace(context.renderSystem.Resources());
    if (!m_viewPort.IsValid())
        m_viewPort = render::ViewPort(context.renderSystem.Resources(), Size(512, 512));

    // A widget interaction ends when nothing is active anymore: the next edit is a new undo step
    m_editInProgress = m_editInProgress && ImGui::IsAnyItemActive();

    String title = "Model Editor";
    if (m_import) {
        title += " - Import ";
        title += m_import->path.Filename().c_str();
    } else if (m_model) {
        title += " - ";
        title += m_model->name.empty() ? String("Unnamed") : m_model->name;
        if (m_dirty)
            title += "*";
    }
    title += "###Model Editor";

    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &m_visible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (m_requestedSceneModel) {
        const auto handler = *m_requestedSceneModel;
        m_requestedSceneModel.reset();
        RunDiscarding([this, handler](ImGuiWindowContext& ctx) { OpenFromScene(ctx, handler); });
    }

    DrawMenuBar(context);
    DrawShortcuts(context);

    // Actions chosen in menus and popups run here, at window level, so the dialogs they open
    // live in the same ID scope as the place that draws them
    if (m_deferredAction) {
        auto action = std::move(m_deferredAction);
        m_deferredAction = {};
        action(context);
    }

    if (!m_status.empty())
        ImGui::TextWrapped("%s", m_status.c_str());

    if (m_import || m_model) {
        const float propertiesWidth = (std::min)(380.0f, ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::BeginChild("ModelProperties", ImVec2(propertiesWidth, 0.0f), true);
        if (m_import)
            DrawImport(context);
        else
            DrawProperties(context);
        ImGui::EndChild();
        ImGui::SameLine();

        UpdatePreview();
        ImGui::BeginChild("ModelPreview", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawPreview(context);
        ImGui::EndChild();
    } else {
        DrawEmptyState();
    }

    HandleFileDialog(context);
    DrawDiscardPopup(context);
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Documents
// ─────────────────────────────────────────────────────────────────────────────

void ModelEditorWindow::SetDocument(graphics::Model model, Path filePath, graphics::ModelHandler sceneModel, String status)
{
    CloseDocument();
    m_model.emplace(std::move(model));
    m_filePath = std::move(filePath);
    m_sceneModel = sceneModel;
    m_status = std::move(status);
    m_previewDirty = true;
    m_cameraYaw = 0.6f;
    m_cameraPitch = 0.4f;
    m_cameraZoom = 1.0f;
}

void ModelEditorWindow::CloseDocument()
{
    if (m_previewModels)
        m_previewModels->Remove(m_previewModel);
    m_previewModel = {};
    m_import.reset();
    m_model.reset();
    m_filePath.Clear();
    m_sceneModel = {};
    m_dirty = false;
    m_selected = 0;
    m_undo.clear();
    m_redo.clear();
    m_lastEditKey.clear();
    m_editInProgress = false;
    m_status.clear();
}

void ModelEditorWindow::OpenFile(const Path& path)
{
    auto model = resmgr::Serializer::LoadModel(path);
    if (!model) {
        m_status = model.error().message;
        return;
    }
    if (model->name.empty())
        model->name = String(path.Stem().c_str());
    SetDocument(std::move(*model), path, {}, "Opened " + String(path.string().c_str()) + ".");
}

void ModelEditorWindow::ImportGltf(const Path& path)
{
    auto asset = resmgr::GltfExporter::LoadScene(path);
    if (!asset) {
        m_status = asset.error().message;
        return;
    }

    ImportSession session;
    session.path = path;
    session.options.name = String(path.Stem().c_str());
    session.selected.assign(asset->nodes.size(), true);
    session.asset = std::move(*asset);
    // A glTF node gets a Model component for its mesh only
    const auto* meshNodeModels = session.asset.components.FindComponents<ModelComponent>();
    const size_t meshNodes = meshNodeModels ? meshNodeModels->Size() : 0;

    // A single mesh needs no splitting: open it right away
    if (meshNodes <= 1) {
        auto results = graphics::BuildSceneImport(session.asset, session.selected, session.options);
        if (results.empty()) {
            m_status = "glTF scene contains no meshes.";
            return;
        }
        const size_t count = results.front().model.submeshes.size();
        SetDocument(std::move(results.front().model), {}, {}, "Imported " + ToString(count) + " submeshes from glTF.");
        m_dirty = true;
        return;
    }

    CloseDocument();
    m_import.emplace(std::move(session));
    m_status = "glTF scene with " + ToString(meshNodes) + " mesh nodes: choose how to split it into models.";
    m_cameraYaw = 0.6f;
    m_cameraPitch = 0.4f;
    m_cameraZoom = 1.0f;
}

void ModelEditorWindow::OpenImportedResult()
{
    auto& results = m_import->results;
    if (m_import->selectedResult >= results.size())
        return;
    auto model = std::move(results[m_import->selectedResult].model);
    const size_t count = model.submeshes.size();
    SetDocument(std::move(model), {}, {}, "Imported " + ToString(count) + " submeshes from glTF.");
    m_dirty = true;
}

void ModelEditorWindow::SaveImported(const Path& anyFileInDirectory)
{
    const Path directory = anyFileInDirectory.Parent();
    Set<String> fileNames;
    size_t saved = 0;
    for (const auto& imported : m_import->results) {
        const String baseName = MakeFileName(imported.model.name);
        const String stem = baseName.substr(0, baseName.size() - StringView(".model").size());
        String fileName = baseName;
        for (size_t suffix = 1; !fileNames.insert(fileName).second; ++suffix)
            fileName = stem + "_" + ToString(suffix) + ".model";

        auto result = resmgr::Serializer::SaveModel(imported.model, directory / fileName);
        if (!result) {
            m_status = result.error().message;
            return;
        }
        ++saved;
    }
    m_status = "Saved " + ToString(saved) + " models to " + String(directory.string().c_str()) + ".";
}

void ModelEditorWindow::AddImportedToScene(ImGuiWindowContext& context)
{
    const auto& session = *m_import;
    const auto& sourceNodes = session.asset.nodes;
    auto& scene = context.scene;

    // Every model goes to the scene once; the glTF nodes using them keep their hierarchy
    Vector<graphics::ModelHandler> nodeModels(sourceNodes.size());
    Vector<graphics::ModelHandler> originModels;
    for (const auto& imported : session.results) {
        const auto handler = context.models.Insert(imported.model);
        for (const size_t nodeIndex : imported.instanceNodes) {
            if (nodeIndex < sourceNodes.size())
                nodeModels[nodeIndex] = handler;
            else
                originModels.push_back(handler);
        }
    }

    // A node is created when it holds a model or is an ancestor of one
    Vector<bool> needed(sourceNodes.size(), false);
    for (size_t nodeIndex = 0; nodeIndex < sourceNodes.size(); ++nodeIndex) {
        if (!nodeModels[nodeIndex])
            continue;
        for (size_t current = nodeIndex; current < sourceNodes.size() && !needed[current]; current = sourceNodes[current].parentIndex)
            needed[current] = true;
    }

    // One root for the whole import: the set moves as a whole, each model stays a node of its own
    const auto rootName = String(session.path.Stem().c_str());
    const NodeId root = scene.AddNode({ .name = rootName });
    auto& sceneModels = scene.Components<ModelComponent>();

    size_t created = 0;
    for (const auto handler : originModels) {
        const NodeId node = scene.AddNode({ .name = context.models.Get(handler)->name, .parent = root });
        sceneModels.Emplace(node, ModelComponent { .model = handler });
        ++created;
    }

    // Parents precede their children in the asset, so they are already in the scene when needed
    Vector<NodeId> sceneNodes(sourceNodes.size());
    for (size_t nodeIndex = 0; nodeIndex < sourceNodes.size(); ++nodeIndex) {
        if (!needed[nodeIndex])
            continue;
        const auto& source = sourceNodes[nodeIndex];
        const NodeId parent = source.parentIndex < nodeIndex && sceneNodes[source.parentIndex]
            ? sceneNodes[source.parentIndex]
            : root;
        sceneNodes[nodeIndex] = scene.AddNode({
            .name = source.name.empty() ? "Node " + ToString(nodeIndex) : source.name,
            .parent = parent,
            .localTransform = source.localTransform,
        });
        if (nodeModels[nodeIndex])
            sceneModels.Emplace(sceneNodes[nodeIndex], ImportedNodeModel(session.asset, nodeIndex, nodeModels[nodeIndex]));
        ++created;
    }

    m_status = "Added " + ToString(session.results.size()) + " models as " + ToString(created)
        + " nodes under \"" + rootName + "\", with the glTF hierarchy.";
}

void ModelEditorWindow::OpenFromScene(ImGuiWindowContext& context, graphics::ModelHandler handler)
{
    const auto* model = context.models.Get(handler);
    if (!model) {
        m_status = "The scene model no longer exists.";
        return;
    }
    // A copy shares the geometry with the scene model, editing creates new geometry
    SetDocument(*model, {}, handler, "Editing a scene model: Apply to Scene updates every node that uses it.");
}

void ModelEditorWindow::Append(const Path& path, bool gltf)
{
    auto source = gltf ? resmgr::GltfExporter::LoadModel(path) : resmgr::Serializer::LoadModel(path);
    if (!source) {
        m_status = source.error().message;
        return;
    }
    RecordUndo("append");
    m_selected = m_model->submeshes.size();
    for (auto& submesh : source->submeshes)
        m_model->submeshes.push_back(std::move(submesh));
    m_status = "Appended " + ToString(source->submeshes.size()) + " submeshes.";
}

bool ModelEditorWindow::SaveTo(const Path& path)
{
    auto result = resmgr::Serializer::SaveModel(*m_model, path);
    if (!result) {
        m_status = result.error().message;
        return false;
    }
    m_filePath = path;
    m_dirty = false;
    m_status = "Saved to " + String(path.string().c_str()) + ".";
    return true;
}

void ModelEditorWindow::Save()
{
    if (m_filePath.Empty())
        OpenFileDialog(DialogAction::SaveAs);
    else
        SaveTo(m_filePath);
}

bool ModelEditorWindow::IsSceneModelAlive(const ImGuiWindowContext& context) const
{
    return context.models.Get(m_sceneModel) != nullptr;
}

void ModelEditorWindow::ApplyToScene(ImGuiWindowContext& context)
{
    if (!context.models.Replace(m_sceneModel, *m_model)) {
        m_status = "The scene model no longer exists, use Add to Scene.";
        return;
    }
    if (m_filePath.Empty())
        m_dirty = false;
    m_status = "Applied to the scene model.";
}

void ModelEditorWindow::AddToScene(ImGuiWindowContext& context)
{
    // Place the model in front of the scene camera, far enough to see it whole
    const AABB bounds = m_model->GetLocalBounds();
    const float radius = IsEmpty(bounds) ? 0.5f : (std::max)(bounds.GetExtent().Length(), 0.5f);
    const Vector3 position = context.camera.GetPosition() + context.camera.GetForwardDirection() * (radius * 3.0f);

    const String name = m_model->name.empty() ? String("Model") : m_model->name;
    m_sceneModel = context.models.Insert(*m_model);
    const NodeId node = context.scene.AddNode({ .name = name, .localTransform = Matrix4x4::Translation(position) });
    context.scene.Components<ModelComponent>().Emplace(node, ModelComponent { .model = m_sceneModel });
    m_status = "Added \"" + name + "\" to the scene, Apply to Scene now updates it.";
}

void ModelEditorWindow::RunDiscarding(Action action)
{
    if (m_dirty) {
        m_pendingAction = std::move(action);
        m_openDiscardPopup = true;
    } else {
        m_deferredAction = std::move(action);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// History
// ─────────────────────────────────────────────────────────────────────────────

void ModelEditorWindow::RecordUndo(StringView key, bool continuous)
{
    const bool sameInteraction = continuous && m_editInProgress && m_lastEditKey == key;
    if (!sameInteraction) {
        m_undo.push_back(*m_model);
        if (m_undo.size() > MaxUndoSteps)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
    }
    m_lastEditKey = String(key);
    m_editInProgress = continuous;
    m_dirty = true;
    m_previewDirty = true;
}

void ModelEditorWindow::Undo()
{
    if (m_undo.empty())
        return;
    m_redo.push_back(std::move(*m_model));
    m_model = std::move(m_undo.back());
    m_undo.pop_back();
    m_editInProgress = false;
    m_dirty = true;
    m_previewDirty = true;
}

void ModelEditorWindow::Redo()
{
    if (m_redo.empty())
        return;
    m_undo.push_back(std::move(*m_model));
    m_model = std::move(m_redo.back());
    m_redo.pop_back();
    m_editInProgress = false;
    m_dirty = true;
    m_previewDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry operations
// ─────────────────────────────────────────────────────────────────────────────

void ModelEditorWindow::TransformGeometry(const Matrix4x4& transform, bool selectedOnly, StringView description)
{
    RecordUndo("transform");
    auto& submeshes = m_model->submeshes;
    for (size_t index = 0; index < submeshes.size(); ++index) {
        auto& submesh = submeshes[index];
        if (!submesh.data || (selectedOnly && index != m_selected))
            continue;
        submesh.data = MakeShared<const MeshData>(graphics::TransformMeshData(*submesh.data, transform));
    }
    m_status = String(description);
}

void ModelEditorWindow::MovePivot(bool bottom)
{
    const AABB bounds = m_model->GetLocalBounds();
    if (IsEmpty(bounds))
        return;
    Vector3 pivot = bounds.GetCenter();
    if (bottom)
        pivot.y = bounds.minBounds.y;
    TransformGeometry(Matrix4x4::Translation(pivot * -1.0f), false,
        bottom ? "Pivot moved to the bottom center." : "Pivot moved to the bounds center.");
}

void ModelEditorWindow::FlipFaces(bool selectedOnly)
{
    RecordUndo("flip");
    auto& submeshes = m_model->submeshes;
    for (size_t index = 0; index < submeshes.size(); ++index) {
        auto& submesh = submeshes[index];
        if (submesh.data && (!selectedOnly || index == m_selected))
            submesh.data = MakeShared<const MeshData>(FlipMeshFaces(*submesh.data));
    }
    m_status = "Faces flipped.";
}

void ModelEditorWindow::MergeByColor()
{
    auto& submeshes = m_model->submeshes;
    Vector<graphics::Model::Submesh> merged;
    Vector<MeshData> geometry;
    for (const auto& submesh : submeshes) {
        if (!submesh.data)
            continue;
        const auto same = std::find_if(merged.begin(), merged.end(), [&](const graphics::Model::Submesh& candidate) {
            const auto& a = candidate.color;
            const auto& b = submesh.color;
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        });
        if (same == merged.end()) {
            merged.push_back({ {}, submesh.color, submesh.name });
            geometry.push_back(*submesh.data);
        } else {
            AppendMeshData(geometry[static_cast<size_t>(same - merged.begin())], *submesh.data);
        }
    }
    if (merged.size() == submeshes.size()) {
        m_status = "Every submesh already has its own color, nothing to merge.";
        return;
    }

    RecordUndo("merge");
    const size_t before = submeshes.size();
    for (size_t index = 0; index < merged.size(); ++index)
        merged[index].data = MakeShared<const MeshData>(std::move(geometry[index]));
    submeshes = std::move(merged);
    m_selected = 0;
    m_status = "Merged " + ToString(before) + " submeshes into " + ToString(submeshes.size()) + ".";
}

void ModelEditorWindow::DuplicateSelected()
{
    auto& submeshes = m_model->submeshes;
    if (m_selected >= submeshes.size())
        return;
    RecordUndo("duplicate");
    auto copy = submeshes[m_selected];
    copy.name += " Copy";
    submeshes.insert(submeshes.begin() + static_cast<ptrdiff_t>(m_selected) + 1, std::move(copy));
    ++m_selected;
}

void ModelEditorWindow::DeleteSelected()
{
    auto& submeshes = m_model->submeshes;
    if (m_selected >= submeshes.size())
        return;
    RecordUndo("delete");
    submeshes.erase(submeshes.begin() + static_cast<ptrdiff_t>(m_selected));
    if (m_selected >= submeshes.size() && m_selected > 0)
        --m_selected;
}

void ModelEditorWindow::MoveSelected(int direction)
{
    auto& submeshes = m_model->submeshes;
    const auto target = static_cast<ptrdiff_t>(m_selected) + direction;
    if (m_selected >= submeshes.size() || target < 0 || target >= static_cast<ptrdiff_t>(submeshes.size()))
        return;
    RecordUndo("reorder");
    std::swap(submeshes[m_selected], submeshes[static_cast<size_t>(target)]);
    m_selected = static_cast<size_t>(target);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────────────────────

void ModelEditorWindow::OpenFileDialog(DialogAction action)
{
    m_dialogAction = action;
    switch (action) {
    case DialogAction::Open:
        m_fileDialog.Open("Open Engine Model", ".model", AssetPaths::Models());
        break;
    case DialogAction::Import:
        m_fileDialog.Open("Import glTF Model", ".gltf");
        break;
    case DialogAction::AppendGltf:
        m_fileDialog.Open("Append Submeshes from glTF", ".gltf");
        break;
    case DialogAction::AppendModel:
        m_fileDialog.Open("Append Submeshes from Model", ".model", AssetPaths::Models());
        break;
    case DialogAction::SaveImported:
        m_fileDialog.SaveAs("Save Models (choose the folder)", ".model",
            m_import && !m_import->results.empty() ? MakeFileName(m_import->results.front().model.name) : String("model.model"),
            AssetPaths::Models());
        break;
    case DialogAction::SaveAs:
        m_fileDialog.SaveAs("Save Engine Model", ".model",
            m_filePath.Empty() ? MakeFileName(m_model->name) : String(m_filePath.Filename().c_str()),
            m_filePath.Empty() ? AssetPaths::Models() : m_filePath.Parent());
        break;
    }
}

void ModelEditorWindow::HandleFileDialog(ImGuiWindowContext& context)
{
    const auto selectedPath = m_fileDialog.Draw();
    if (!selectedPath)
        return;
    switch (m_dialogAction) {
    case DialogAction::Open:
        OpenFile(*selectedPath);
        break;
    case DialogAction::Import:
        ImportGltf(*selectedPath);
        break;
    case DialogAction::AppendGltf:
        if (m_model)
            Append(*selectedPath, true);
        break;
    case DialogAction::AppendModel:
        if (m_model)
            Append(*selectedPath, false);
        break;
    case DialogAction::SaveAs:
        if (m_model)
            SaveTo(*selectedPath);
        break;
    case DialogAction::SaveImported:
        if (m_import)
            SaveImported(*selectedPath);
        break;
    }
    (void)context;
}

void ModelEditorWindow::DrawDiscardPopup(ImGuiWindowContext& context)
{
    (void)context;
    constexpr const char* popupName = "Unsaved Model Changes";
    if (m_openDiscardPopup) {
        ImGui::OpenPopup(popupName);
        m_openDiscardPopup = false;
    }
    if (!ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("The model has unsaved changes. Discard them?");
    if (ImGui::Button("Discard")) {
        m_dirty = false;
        m_deferredAction = std::move(m_pendingAction);
        m_pendingAction = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_pendingAction = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ModelEditorWindow::DrawMenuBar(ImGuiWindowContext& context)
{
    if (!ImGui::BeginMenuBar())
        return;

    const bool hasModel = m_model.has_value();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New"))
            RunDiscarding([this](ImGuiWindowContext&) {
                SetDocument(graphics::Model("New Model"), {}, {}, "New model.");
            });
        if (ImGui::MenuItem("Open .model..."))
            RunDiscarding([this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::Open); });
        if (ImGui::MenuItem("Import glTF..."))
            RunDiscarding([this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::Import); });
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasModel))
            m_deferredAction = [this](ImGuiWindowContext&) { Save(); };
        if (ImGui::MenuItem("Save As...", nullptr, false, hasModel))
            m_deferredAction = [this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::SaveAs); };
        ImGui::Separator();
        if (ImGui::MenuItem("Close", nullptr, false, hasModel))
            RunDiscarding([this](ImGuiWindowContext&) { CloseDocument(); });
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit", hasModel)) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty()))
            Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty()))
            Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Add Cube Submesh")) {
            RecordUndo("add");
            m_model->AddSubmesh(GeometryPrimitives::CreateCube(1.0f), Vector4 { 0.8f, 0.8f, 0.8f, 1.0f }, "Cube");
            m_selected = m_model->submeshes.size() - 1;
        }
        if (ImGui::MenuItem("Append from glTF..."))
            m_deferredAction = [this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::AppendGltf); };
        if (ImGui::MenuItem("Append from .model..."))
            m_deferredAction = [this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::AppendModel); };
        ImGui::Separator();
        if (ImGui::MenuItem("Merge Submeshes by Color"))
            MergeByColor();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene", hasModel)) {
        if (ImGui::MenuItem("Apply to Scene", nullptr, false, IsSceneModelAlive(context)))
            ApplyToScene(context);
        if (ImGui::MenuItem("Add to Scene"))
            AddToScene(context);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void ModelEditorWindow::DrawShortcuts(ImGuiWindowContext& context)
{
    const auto& io = ImGui::GetIO();
    if (!m_model || !io.KeyCtrl || io.WantTextInput || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift)
            Redo();
        else
            Undo();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        Redo();
    if (ImGui::IsKeyPressed(ImGuiKey_S, false))
        m_deferredAction = [this](ImGuiWindowContext&) { Save(); };
    (void)context;
}

void ModelEditorWindow::DrawEmptyState()
{
    ImGui::Spacing();
    ImGui::TextDisabled("No model is open.");
    ImGui::TextDisabled("Create a model, open a .model file, import a glTF file");
    ImGui::TextDisabled("or press \"Edit Model\" on a node in the Scene Hierarchy.");
    ImGui::Spacing();
    if (ImGui::Button("New"))
        SetDocument(graphics::Model("New Model"), {}, {}, "New model.");
    ImGui::SameLine();
    if (ImGui::Button("Open .model..."))
        OpenFileDialog(DialogAction::Open);
    ImGui::SameLine();
    if (ImGui::Button("Import glTF..."))
        OpenFileDialog(DialogAction::Import);
}

void ModelEditorWindow::DrawProperties(ImGuiWindowContext& context)
{
    if (auto name = InputString("Name", m_model->name)) {
        RecordUndo("model name", true);
        m_model->name = std::move(*name);
    }

    if (!m_filePath.Empty())
        ImGui::TextDisabled("File: %s", m_filePath.string().c_str());
    if (m_sceneModel) {
        if (IsSceneModelAlive(context)) {
            const auto* nodeModels = context.scene.FindComponents<ModelComponent>();
            const auto components = nodeModels ? nodeModels->Components() : std::span<const ModelComponent> { };
            const auto users = std::count_if(components.begin(), components.end(),
                [&](const ModelComponent& component) { return component.model == m_sceneModel; });
            ImGui::TextDisabled("Scene model, used by %zu node(s)", static_cast<size_t>(users));
        } else {
            ImGui::TextDisabled("Scene model was removed from the scene");
        }
    }

    const AABB bounds = m_model->GetLocalBounds();
    size_t vertices = 0;
    for (const auto& submesh : m_model->submeshes)
        vertices += submesh.data ? submesh.data->vertices.size() : 0;
    ImGui::Text("%zu submeshes, %zu vertices, %zu triangles", m_model->submeshes.size(), vertices, CountTriangles(*m_model));
    if (!IsEmpty(bounds)) {
        const Vector3 size = bounds.maxBounds - bounds.minBounds;
        ImGui::Text("Size: %.3f x %.3f x %.3f", size.x, size.y, size.z);
    }

    if (ImGui::Button("Save"))
        m_deferredAction = [this](ImGuiWindowContext&) { Save(); };
    ImGui::SameLine();
    if (IsSceneModelAlive(context)) {
        if (ImGui::Button("Apply to Scene"))
            ApplyToScene(context);
        ImGui::SameLine();
    }
    if (ImGui::Button("Add to Scene"))
        AddToScene(context);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button("Undo"))
        Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button("Redo"))
        Redo();
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Submeshes", ImGuiTreeNodeFlags_DefaultOpen))
        DrawSubmeshes();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        DrawTransformTools();
}

void ModelEditorWindow::DrawSubmeshes()
{
    auto& submeshes = m_model->submeshes;
    if (m_selected >= submeshes.size())
        m_selected = submeshes.empty() ? 0 : submeshes.size() - 1;

    if (ImGui::Checkbox("Highlight", &m_highlightSelected))
        m_previewDirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Solo", &m_soloSelected))
        m_previewDirty = true;

    ImGui::BeginChild("SubmeshList", ImVec2(0.0f, 160.0f), true);
    for (size_t index = 0; index < submeshes.size(); ++index) {
        const auto& submesh = submeshes[index];
        ImGui::PushID(static_cast<int>(index));
        const ImVec4 swatch { submesh.color.x, submesh.color.y, submesh.color.z, submesh.color.w };
        ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
            ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        const String label = (submesh.name.empty() ? "Submesh " + ToString(index) : submesh.name)
            + "  (" + ToString(submesh.data ? submesh.data->indices.size() / 3 : 0) + " tris)";
        if (ImGui::Selectable(label.c_str(), m_selected == index)) {
            m_selected = index;
            m_previewDirty = true;
        }
        ImGui::PopID();
    }
    if (submeshes.empty())
        ImGui::TextDisabled("No submeshes. Use Edit > Add or Append.");
    ImGui::EndChild();

    const bool hasSelection = m_selected < submeshes.size();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Up"))
        MoveSelected(-1);
    ImGui::SameLine();
    if (ImGui::Button("Down"))
        MoveSelected(1);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate"))
        DuplicateSelected();
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        DeleteSelected();
    ImGui::EndDisabled();

    if (m_selected >= submeshes.size())
        return;

    ImGui::PushID("SelectedSubmesh");
    auto& selected = submeshes[m_selected];
    const String key = ToString(m_selected);
    if (auto name = InputString("Submesh name", selected.name)) {
        RecordUndo("submesh name " + key, true);
        submeshes[m_selected].name = std::move(*name);
    }
    float color[4] = { selected.color.x, selected.color.y, selected.color.z, selected.color.w };
    if (ImGui::ColorEdit4("Color", color)) {
        RecordUndo("submesh color " + key, true);
        submeshes[m_selected].color = Vector4 { color[0], color[1], color[2], color[3] };
    }
    if (const auto& data = submeshes[m_selected].data)
        ImGui::TextDisabled("%zu vertices, %zu triangles", data->vertices.size(), data->indices.size() / 3);
    ImGui::PopID();
}

void ModelEditorWindow::DrawTransformTools()
{
    ImGui::PushID("TransformTools");
    ImGui::DragFloat3("Translate", &m_toolTranslation.x, 0.01f);
    ImGui::DragFloat3("Rotate (deg)", &m_toolRotation.x, 0.5f);
    ImGui::DragFloat3("Scale", &m_toolScale.x, 0.01f);
    ImGui::Checkbox("Selected submesh only", &m_toolSelectedOnly);
    if (ImGui::Button("Apply Transform")) {
        TransformGeometry(MakeTransform(m_toolTranslation, m_toolRotation, m_toolScale),
            m_toolSelectedOnly, "Transform applied.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Fields")) {
        m_toolTranslation = { 0.0f, 0.0f, 0.0f };
        m_toolRotation = { 0.0f, 0.0f, 0.0f };
        m_toolScale = { 1.0f, 1.0f, 1.0f };
    }

    ImGui::SeparatorText("Quick operations");
    if (ImGui::Button("Center Pivot"))
        MovePivot(false);
    ImGui::SameLine();
    if (ImGui::Button("Pivot to Bottom"))
        MovePivot(true);
    if (ImGui::Button("Z-up to Y-up"))
        TransformGeometry(Matrix4x4::RotationX(-std::numbers::pi_v<float> * 0.5f), m_toolSelectedOnly, "Converted from Z-up to Y-up.");
    ImGui::SameLine();
    if (ImGui::Button("Mirror Z"))
        TransformGeometry(Matrix4x4::Scaling(Vector3 { 1.0f, 1.0f, -1.0f }), m_toolSelectedOnly, "Mirrored along Z.");
    ImGui::SameLine();
    if (ImGui::Button("Flip Faces"))
        FlipFaces(m_toolSelectedOnly);
    ImGui::PopID();
}

void ModelEditorWindow::DrawImport(ImGuiWindowContext& context)
{
    auto& session = *m_import;
    ImGui::TextDisabled("%s", session.path.string().c_str());

    auto& options = session.options;
    int mode = static_cast<int>(options.mode);
    if (ImGui::Combo("Mode", &mode, "Merge into one model\0One model per mesh\0One model per node group\0")) {
        options.mode = static_cast<graphics::SceneImportMode>(mode);
        session.resultsDirty = true;
    }
    switch (options.mode) {
    case graphics::SceneImportMode::MergeAll:
        ImGui::TextDisabled("Selected meshes are baked into one model in scene space.");
        if (auto name = InputString("Model name", options.name)) {
            options.name = std::move(*name);
            session.resultsDirty = true;
        }
        break;
    case graphics::SceneImportMode::ModelPerMesh:
        ImGui::TextDisabled("Every glTF mesh is a model, nodes using it become its instances.");
        break;
    case graphics::SceneImportMode::ModelPerGroup: {
        ImGui::TextDisabled("Nodes at the group depth are models with the meshes below them.");
        const auto depths = graphics::ComputeNodeDepths(session.asset.nodes);
        const int maxDepth = depths.empty() ? 0 : static_cast<int>(*std::max_element(depths.begin(), depths.end()));
        int depth = static_cast<int>(options.groupDepth);
        if (ImGui::SliderInt("Group depth", &depth, 0, maxDepth)) {
            options.groupDepth = static_cast<uint32_t>(depth);
            session.resultsDirty = true;
        }
        break;
    }
    }

    ImGui::SeparatorText("Nodes");
    DrawImportTree();

    if (session.resultsDirty) {
        session.results = graphics::BuildSceneImport(session.asset, session.selected, options);
        session.resultsDirty = false;
        session.selectedResult = (std::min)(session.selectedResult, session.results.empty() ? size_t { 0 } : session.results.size() - 1);
        m_previewDirty = true;
    }

    ImGui::SeparatorText("Result");
    ImGui::BeginChild("ImportResults", ImVec2(0.0f, 140.0f), true);
    for (size_t index = 0; index < session.results.size(); ++index) {
        const auto& imported = session.results[index];
        const String label = imported.model.name + "  (" + ToString(imported.model.submeshes.size()) + " submeshes, "
            + ToString(CountTriangles(imported.model)) + " tris, " + ToString(imported.instanceNodes.size())
            + (imported.instanceNodes.size() == 1 ? " instance)" : " instances)") + "##result" + ToString(index);
        if (ImGui::Selectable(label.c_str(), session.selectedResult == index)) {
            session.selectedResult = index;
            m_previewDirty = true;
        }
    }
    if (session.results.empty())
        ImGui::TextDisabled("Nothing selected.");
    ImGui::EndChild();

    const bool hasResults = !session.results.empty();
    ImGui::BeginDisabled(!hasResults);
    if (ImGui::Button("Open in Editor"))
        m_deferredAction = [this](ImGuiWindowContext&) { OpenImportedResult(); };
    ImGui::SameLine();
    if (ImGui::Button("Save All..."))
        m_deferredAction = [this](ImGuiWindowContext&) { OpenFileDialog(DialogAction::SaveImported); };
    ImGui::SameLine();
    if (ImGui::Button("Add All to Scene"))
        AddImportedToScene(context);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        m_deferredAction = [this](ImGuiWindowContext&) { CloseDocument(); };
    ImGui::TextDisabled("Open in Editor edits the selected model and ends the import.");
}

void ModelEditorWindow::DrawImportTree()
{
    auto& session = *m_import;
    const auto& asset = session.asset;
    const auto& nodes = asset.nodes;

    if (ImGui::SmallButton("All")) {
        std::fill(session.selected.begin(), session.selected.end(), true);
        session.resultsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) {
        std::fill(session.selected.begin(), session.selected.end(), false);
        session.resultsDirty = true;
    }

    Vector<Vector<size_t>> children(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].parentIndex < nodes.size())
            children[nodes[index].parentIndex].push_back(index);
    }
    const auto setSubtree = [&](auto&& self, size_t index, bool value) -> void {
        session.selected[index] = value;
        for (const size_t child : children[index])
            self(self, child, value);
    };

    ImGui::BeginChild("ImportNodes", ImVec2(0.0f, 200.0f), true);
    const auto drawNode = [&](auto&& self, size_t index) -> void {
        const auto& node = nodes[index];
        ImGui::PushID(static_cast<int>(index));
        bool selected = session.selected[index];
        if (ImGui::Checkbox("##selected", &selected)) {
            setSubtree(setSubtree, index, selected);
            session.resultsDirty = true;
        }
        ImGui::SameLine();

        String label = node.name.empty() ? "Node " + ToString(index) : node.name;
        const size_t meshIndex = asset.ModelIndexOfNode(index);
        if (meshIndex < asset.models.size()) {
            const auto& mesh = asset.models[meshIndex];
            label += "  [" + (mesh.name.empty() ? "mesh " + ToString(meshIndex) : mesh.name) + ", "
                + ToString(CountTriangles(mesh)) + " tris]";
        }
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children[index].empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        ImGui::PopID();
        if (!open || children[index].empty())
            return;
        for (const size_t child : children[index])
            self(self, child);
        ImGui::TreePop();
    };
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].parentIndex >= nodes.size())
            drawNode(drawNode, index);
    }
    ImGui::EndChild();
}

void ModelEditorWindow::DrawPreview(ImGuiWindowContext& context)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const auto texture = ToTextureId(m_viewPort.GetColorTexture());
    if (!texture || available.x <= 0.0f || available.y <= 0.0f)
        return;

    const auto* viewport = ImGui::GetWindowViewport();
    const float dpiScale = viewport ? viewport->DpiScale : 1.0f;
    m_viewPort.SetSize({
        static_cast<uint32_t>(available.x * dpiScale),
        static_cast<uint32_t>(available.y * dpiScale)
    });

    const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
    ImGui::Image(texture, available);
    if (ImGui::IsItemHovered()) {
        const auto& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_cameraYaw += io.MouseDelta.x * 0.01f;
            m_cameraPitch = std::clamp(m_cameraPitch + io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
        }
        if (io.MouseWheel != 0.0f)
            m_cameraZoom = std::clamp(m_cameraZoom * std::pow(0.9f, io.MouseWheel), 0.1f, 20.0f);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_cameraYaw = 0.6f;
            m_cameraPitch = 0.4f;
            m_cameraZoom = 1.0f;
        }
    }
    ImGui::GetWindowDrawList()->AddText(ImVec2(imagePosition.x + 6.0f, imagePosition.y + 4.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), "LMB drag: orbit   Wheel: zoom   Double click: reset");

    // Orbit camera framing the whole model, so the view stays put while submeshes are soloed
    const auto* source = GetPreviewSource();
    AABB bounds = source ? source->GetLocalBounds() : AABB {};
    if (IsEmpty(bounds))
        bounds = AABB { Vector3 { -0.5f, -0.5f, -0.5f }, Vector3 { 0.5f, 0.5f, 0.5f } };
    const Vector3 center = bounds.GetCenter();
    const float radius = (std::max)(bounds.GetExtent().Length(), 0.001f);
    constexpr float fov = std::numbers::pi_v<float> / 3.0f;
    const float distance = radius / std::sin(fov * 0.5f) * m_cameraZoom;
    const Vector3 direction {
        std::sin(m_cameraYaw) * std::cos(m_cameraPitch),
        std::sin(m_cameraPitch),
        -std::cos(m_cameraYaw) * std::cos(m_cameraPitch)
    };
    const Vector3 eye = center + direction * distance;
    const Matrix4x4 view = Matrix4x4::LookAt(eye, center, Vector3 { 0.0f, 1.0f, 0.0f });
    const Matrix4x4 projection = Matrix4x4::PerspectiveVk(fov, available.x / available.y,
        (std::max)(distance - radius * 1.5f, distance * 0.01f), distance + radius * 1.5f);

    auto& frame = context.frame;
    frame.BeginSceneView({ projection * view, eye }, m_viewPort.GetColorTexture(), m_viewPort.GetSize());
    m_previewModels->ForEachSubmesh(m_previewModel, [&](const graphics::Model::Submesh& submesh, render::MeshHandle mesh) {
        frame.Draw(mesh, Matrix4x4::Identity(), submesh.color);
    });
}

const graphics::Model* ModelEditorWindow::GetPreviewSource() const
{
    if (m_import) {
        const auto& results = m_import->results;
        return m_import->selectedResult < results.size() ? &results[m_import->selectedResult].model : nullptr;
    }
    return m_model ? &*m_model : nullptr;
}

void ModelEditorWindow::UpdatePreview()
{
    if (!m_previewDirty && m_previewModels->Get(m_previewModel))
        return;
    m_previewDirty = false;

    // Same geometry as the source, so unchanged submeshes keep their GPU meshes
    graphics::Model preview;
    if (const auto* source = GetPreviewSource()) {
        // Submesh selection only exists for the edited document
        const bool document = !m_import;
        preview.name = source->name;
        for (size_t index = 0; index < source->submeshes.size(); ++index) {
            const auto& submesh = source->submeshes[index];
            const bool selected = index == m_selected;
            if (!submesh.data || (document && m_soloSelected && !selected))
                continue;
            Vector4 color = submesh.color;
            if (document && m_highlightSelected && !m_soloSelected && !selected)
                color = Vector4 { color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, color.w };
            preview.AddSubmesh(submesh.data, color, submesh.name);
        }
    }
    if (m_previewModels->Get(m_previewModel))
        m_previewModels->Replace(m_previewModel, std::move(preview));
    else
        m_previewModel = m_previewModels->Insert(std::move(preview));
}

} // namespace elm
