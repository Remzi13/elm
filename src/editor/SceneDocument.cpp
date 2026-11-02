#include "editor/SceneDocument.hpp"

#include "editor/SceneEdits.hpp"
#include "editor/ui/EditorServices.hpp"

#include "core/Unexpected.hpp"

#include "model/ModelCatalog.hpp"

#include "resource/AssetImport.hpp"
#include "resource/GltfExporter.hpp"
#include "resource/Serializer.hpp"

#include "Scene/Scene.hpp"

#include "script/EngineContext.hpp"

#include <string>

namespace elm {

namespace {

    using Kind = SceneStatus::Kind;

    String PathText(const Path& path)
    {
        return String(path.string().c_str());
    }

} // namespace

SceneDocument::SceneDocument(const SceneDocumentDeps& deps, Path assets, Path scenes, const Path& file)
    : m_deps(deps)
    , m_assets(std::move(assets))
    , m_scenes(std::move(scenes))
    , m_file(file.Empty() ? UnusedFile() : file)
{
}

String SceneDocument::ShownName() const
{
    const String relative(m_file.LexicallyNormal().LexicallyRelative(m_scenes.LexicallyNormal()).generic_string().c_str());
    return relative.empty() || relative.starts_with("..") ? PathText(m_file) : relative;
}

bool SceneDocument::IsModified() const noexcept
{
    return m_deps.edits.IsModified();
}

bool SceneDocument::IsPlaying() const
{
    return m_deps.play.isPlaying && m_deps.play.isPlaying();
}

bool SceneDocument::RefuseInPlay()
{
    if (!IsPlaying())
        return false;
    Report(Kind::Failure, "Stop first: in Play mode the scene is the running game.");
    return true;
}

void SceneDocument::New()
{
    if (RefuseInPlay())
        return;
    m_deps.scene.Clear();
    m_deps.models.Clear();
    // The steps are about the nodes and models that went
    m_deps.edits.Clear();
    m_deps.selection.node = NodeId { };
    m_file = UnusedFile();
    Report(Kind::Info, "New scene " + ShownName() + ".");
}

void SceneDocument::Open(const Path& file)
{
    if (RefuseInPlay())
        return;
    auto result = resource::Serializer::LoadScene(file, m_assets);
    if (!result) {
        Report(Kind::Failure, result.error().message);
        return;
    }
    m_deps.scene.Clear();
    m_deps.models.Clear();
    const auto handlers = m_deps.models.InsertAll(std::move(result->models), result->modelFiles);
    (void)m_deps.scene.Append(std::move(*result), handlers);
    // The steps are about the nodes and models of the scene before
    m_deps.edits.Clear();
    m_deps.selection.node = NodeId { };
    m_file = file;
    Report(Kind::Success, "Scene loaded from " + PathText(file) + ".");
}

bool SceneDocument::Save()
{
    return SaveAs(m_file);
}

bool SceneDocument::SaveAs(const Path& file)
{
    if (RefuseInPlay())
        return false;
    // The new models into the project's models folder, then the scene that references them
    auto saved = resource::Serializer::SaveNewModels(m_deps.scene, m_deps.models, m_assets);
    if (saved) {
        auto scene = resource::Serializer::SaveScene(m_deps.scene, m_deps.models, m_assets, file);
        if (!scene) {
            Report(Kind::Failure, scene.error().message);
            return false;
        }
    } else {
        Report(Kind::Failure, saved.error().message);
        return false;
    }
    m_file = file;
    m_deps.edits.MarkSaved();
    Report(Kind::Success, "Scene saved to " + PathText(file) + ".");
    return true;
}

void SceneDocument::ImportGltf(const Path& file)
{
    auto result = resource::AssetImport::ImportScene(file, m_assets);
    if (!result) {
        Report(Kind::Failure, "Failed to import glTF scene: " + result.error().message);
        return;
    }
    const auto handlers = m_deps.models.InsertAll(std::move(result->models), result->modelFiles);
    const auto newIds = m_deps.scene.Append(std::move(*result), handlers);
    if (!newIds.empty())
        m_deps.selection.node = newIds.front();
    Report(Kind::Success, "glTF scene imported from " + String(file.Filename().c_str()) + ".");
}

void SceneDocument::ExportGltf(const Path& file)
{
    auto result = resource::GltfExporter::Export(file, m_deps.scene, m_deps.models);
    if (result)
        Report(Kind::Success, "Scene exported to " + PathText(file) + ".");
    else
        Report(Kind::Failure, result.error().message);
}

void SceneDocument::Report(SceneStatus::Kind kind, String text)
{
    m_status = SceneStatus { kind, std::move(text) };
}

Path SceneDocument::UnusedFile() const
{
    String fileName = "new.scene";
    for (size_t suffix = 1; FileSystem::Exists(m_scenes / fileName); ++suffix)
        fileName = "new_" + String(std::to_string(suffix).c_str()) + ".scene";
    return m_scenes / fileName;
}

} // namespace elm
