#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/Scene.hpp"
#include "Scene/SceneAsset.hpp"

namespace elm::project {
    class Project;
}

namespace elm::resmgr {
    class Serializer {
    public:
        [[nodiscard]] static auto SaveModel(const graphics::Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto LoadModel(const Path& path) -> EngineResult<graphics::Model>;
        /// Writes a model into the models folder of an assets folder under a name no file has yet
        /// (models/<name>.model, models/<name>_1.model, ...). Returns the file.
        [[nodiscard]] static auto SaveNewModel(const graphics::Model& model, const Path& assetsFolder) -> EngineResult<Path>;
        /// Writes every model the scene uses that has no file in the assets folder into its models folder, under
        /// a name no file has yet, and gives the model that file (ModelManager::SetFile). Returns how many were written.
        [[nodiscard]] static auto SaveNewModels(const Scene& scene, graphics::ModelManager& models, const Path& assetsFolder)
            -> EngineResult<size_t>;
        /// Saves the scene with references to the files of its models, as paths in the assets folder
        /// (docs/scene/Файл сцены.md); a model without a file there is an error, SaveNewModels comes first.
        [[nodiscard]] static auto SaveScene(const Scene& scene, const graphics::ModelManager& models, const Path& assetsFolder,
            const Path& path) -> EngineResult<void>;
        /// Reads a scene file, every model with its file (SceneAsset::modelFiles); from version 7 the models
        /// are found in the assets folder.
        [[nodiscard]] static auto LoadScene(const Path& path, const Path& assetsFolder) -> EngineResult<SceneAsset>;
        /// Reads a .elmproject file (docs/project/Проекты.md); path is the absolute path of the file.
        [[nodiscard]] static auto LoadProject(const Path& path) -> EngineResult<project::Project>;
        /// Writes the project file (Project::File) from its description.
        [[nodiscard]] static auto SaveProject(const project::Project& project) -> EngineResult<void>;

    private:
        [[nodiscard]] static auto WriteFile(const Path& path, StringView contents) -> EngineResult<void>;
        [[nodiscard]] static auto ReadFile(const Path& path) -> EngineResult<String>;
    };
}