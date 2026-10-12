#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class Serializer {
    public:
        [[nodiscard]] static auto SaveModel(const graphics::Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto LoadModel(const Path& path) -> EngineResult<graphics::Model>;
        /// Saves the scene and every model its nodes use, each model into its own file in modelDirectory.
        [[nodiscard]] static auto SaveScene(const Scene& scene, const graphics::ModelManager& models, const Path& path,
            const Path& modelDirectory) -> EngineResult<void>;
        [[nodiscard]] static auto LoadScene(const Path& path) -> EngineResult<SceneAsset>;

    private:
        [[nodiscard]] static auto WriteFile(const Path& path, StringView contents) -> EngineResult<void>;
        [[nodiscard]] static auto ReadFile(const Path& path) -> EngineResult<String>;
    };
}