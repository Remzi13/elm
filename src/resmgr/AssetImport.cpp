#include "resmgr/AssetImport.hpp"

#include "resmgr/GltfExporter.hpp"
#include "resmgr/JsonValue.hpp"
#include "resmgr/SerializationError.hpp"
#include "resmgr/Serializer.hpp"

#include "project/AssetPath.hpp"
#include "project/Project.hpp"

#include <iterator>
#include <string>
#include <utility>

namespace elm::resmgr {

    namespace {

        constexpr const char* LogSource = "AssetImport";
        /// The parts of a .gltf that may live in files of their own.
        constexpr const char* ReferencingLists[] = { "buffers", "images" };

        auto ReadText(const Path& file) -> EngineResult<String>
        {
            auto stream = FileSystem::OpenForReading(file, std::ios::binary);
            if (!stream)
                return UnexpectedSerializationError("Cannot read " + file.string(), LogSource);
            return String { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        }

        /// The uri of every buffer and image of a .gltf that is a file, not data inside the .gltf.
        auto ReferencedFiles(const Path& gltf) -> EngineResult<Vector<String>>
        {
            auto text = ReadText(gltf);
            if (!text)
                return UnexpectedSerializationError(text.error());
            JsonValue document;
            if (auto parsed = document.Parse(*text); !parsed)
                return UnexpectedSerializationError(parsed.error());
            Vector<String> uris;
            for (const char* list : ReferencingLists) {
                const auto items = document.Find(list);
                const auto count = items && items->IsArray() ? items->Size() : EngineResult<size_t> { 0 };
                for (size_t index = 0; count && index < *count; ++index) {
                    const auto uri = items->At(index).Find("uri");
                    auto value = uri && uri->GetKind() == JsonValue::Kind::String ? uri->AsString() : EngineResult<String> { };
                    if (value && !value->empty() && !value->starts_with("data:"))
                        uris.push_back(std::move(*value));
                }
            }
            return uris;
        }

        /// source/<stem>, with a number added while such a folder exists.
        Path UnusedSourceFolder(const Path& assetsFolder, const String& stem)
        {
            const Path sources = assetsFolder / project::Project::SourceFolder;
            Path folder = sources / stem;
            for (size_t suffix = 1; FileSystem::Exists(folder); ++suffix)
                folder = sources / (stem + "_" + std::to_string(suffix).c_str());
            return folder;
        }

        bool IsGltf(const Path& file)
        {
            const auto extension = file.Extension().string();
            return extension == ".gltf" || extension == ".glb";
        }

        auto LoadOneModel(const Path& file) -> EngineResult<graphics::Model>
        {
            auto model = IsGltf(file) ? GltfExporter::LoadModel(file) : Serializer::LoadModel(file);
            if (model && model->name.empty())
                model->name = String(file.Stem().string().c_str());
            return model;
        }

        auto CopyOne(const Path& from, const Path& to) -> EngineResult<void>
        {
            std::error_code error;
            if (!FileSystem::Copy(from, to, error))
                return UnexpectedSerializationError("Cannot copy " + from.string() + " to " + to.string() + ": " + error.message(),
                    LogSource);
            return { };
        }

    } // namespace

    auto AssetImport::CopyIntoProject(const Path& file, const Path& assetsFolder) -> EngineResult<Path>
    {
        if (project::AssetPath::Of(file, assetsFolder))
            return file;
        Vector<String> references;
        if (file.Extension().string() == ".gltf") {
            auto found = ReferencedFiles(file);
            if (!found)
                return UnexpectedSerializationError(found.error());
            references = std::move(*found);
        }

        const Path folder = UnusedSourceFolder(assetsFolder, String(file.Stem().string().c_str()));
        for (const String& uri : references) {
            // A referenced file keeps its place next to the copy, so it must be below the .gltf's folder:
            // a relative path without ".." is exactly what an asset path is
            if (!project::AssetPath::FromString(uri))
                return UnexpectedSerializationError(String(file.string().c_str()) + " references a file outside of its folder: " + uri,
                    LogSource);
            if (auto copied = CopyOne(file.Parent() / uri, folder / uri); !copied)
                return UnexpectedSerializationError(copied.error());
        }
        Path copy = folder / file.Filename();
        if (auto copied = CopyOne(file, copy); !copied)
            return UnexpectedSerializationError(copied.error());
        return copy;
    }

    auto AssetImport::ImportModel(const Path& file, const Path& assetsFolder) -> EngineResult<ProjectModel>
    {
        auto model = LoadOneModel(file);
        if (!model)
            return UnexpectedSerializationError(model.error());
        if (!IsGltf(file) && project::AssetPath::Of(file, assetsFolder))
            return ProjectModel { std::move(*model), file };
        if (IsGltf(file)) {
            if (auto source = CopyIntoProject(file, assetsFolder); !source)
                return UnexpectedSerializationError(source.error());
        }
        auto saved = Serializer::SaveNewModel(*model, assetsFolder);
        if (!saved)
            return UnexpectedSerializationError(saved.error());
        return ProjectModel { std::move(*model), std::move(*saved) };
    }

    auto AssetImport::ImportScene(const Path& gltf, const Path& assetsFolder) -> EngineResult<SceneAsset>
    {
        auto asset = GltfExporter::LoadScene(gltf);
        if (!asset)
            return UnexpectedSerializationError(asset.error());
        if (auto source = CopyIntoProject(gltf, assetsFolder); !source)
            return UnexpectedSerializationError(source.error());
        asset->modelFiles.clear();
        for (const auto& model : asset->models) {
            auto saved = Serializer::SaveNewModel(model, assetsFolder);
            if (!saved)
                return UnexpectedSerializationError(saved.error());
            asset->modelFiles.push_back(std::move(*saved));
        }
        return asset;
    }

} // namespace elm::resmgr
