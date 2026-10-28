#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/Model.hpp"

#include "Scene/SceneAsset.hpp"

namespace elm::resmgr {

    /// A model kept in the project: the model and its .model file in the assets folder.
    struct ProjectModel {
        graphics::Model model;
        Path file;
    };

    /// Brings files from outside into the project's assets folder (docs/project/Проекты.md#Импорт): the
    /// original of an import is kept in source/, a .gltf together with the buffers and images it references.
    class AssetImport {
    public:
        AssetImport() = delete;

        /// The file inside the assets folder. A file already there is returned as it is; any other is copied
        /// into source/<name>/ (source/<name>_1/ when that folder exists), a .gltf with the files it references.
        [[nodiscard]] static auto CopyIntoProject(const Path& file, const Path& assetsFolder) -> EngineResult<Path>;
        /// Reads a .model, .gltf or .glb as one model kept in the project. A .model of the assets folder stays
        /// that file; for anything else the original goes to source/ and the model is written as a new .model.
        [[nodiscard]] static auto ImportModel(const Path& file, const Path& assetsFolder) -> EngineResult<ProjectModel>;
        /// Reads a glTF scene into the project: the original goes to source/, every model is written as a
        /// new .model (SceneAsset::modelFiles).
        [[nodiscard]] static auto ImportScene(const Path& gltf, const Path& assetsFolder) -> EngineResult<SceneAsset>;
    };

} // namespace elm::resmgr
