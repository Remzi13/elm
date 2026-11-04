#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "model/Model.hpp"

#include "Scene/SceneAsset.hpp"

namespace elm::resource {

    /// A model kept in the project: the model and its .model file in the assets folder.
    struct ProjectModel {
        Model model;
        Path file;
    };

    /// Brings files from outside into the project's assets folder (docs/project/Проекты.md#Импорт): the
    /// original of an import is kept in source/, a .gltf together with the buffers and images it references.
    /// The same file imported again finds what its first import made: the project gets nothing new. A file
    /// is the same by its contents, wherever it lies; a changed one is a new original next to the old.
    class AssetImport {
    public:
        AssetImport() = delete;

        /// The file inside the assets folder. A file already there is returned as it is, as is the copy in
        /// source/ of an original with the same contents; any other is copied into source/<name>/
        /// (source/<name>_1/ when that folder holds another original), a .gltf with the files it references.
        [[nodiscard]] static auto CopyIntoProject(const Path& file, const Path& assetsFolder) -> EngineResult<Path>;
        /// Reads a .model, .gltf or .glb as one model kept in the project. A .model of the assets folder stays
        /// that file; for anything else the original goes to source/ and the model to a .model
        /// (Serializer::SaveModelOnce): a new one unless a file there holds exactly this model.
        [[nodiscard]] static auto ImportModel(const Path& file, const Path& assetsFolder) -> EngineResult<ProjectModel>;
        /// Reads a glTF scene into the project: the original goes to source/, every model to a .model as in
        /// ImportModel (SceneAsset::modelFiles).
        [[nodiscard]] static auto ImportScene(const Path& gltf, const Path& assetsFolder) -> EngineResult<SceneAsset>;
    };

} // namespace elm::resource
