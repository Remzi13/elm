#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "resmgr/ModelManager.hpp"

#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class Serializer {
    public:
        [[nodiscard]] static auto SaveModel(const Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto SaveScene(const Scene& scene, const ModelManager& models, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto LoadScene(Scene& scene, ModelManager& models,
            render::RenderResources& resources, const Path& path) -> EngineResult<void>;

    private:
        [[nodiscard]] static auto WriteFile(const Path& path, StringView contents) -> EngineResult<void>;
        [[nodiscard]] static auto ReadFile(const Path& path) -> EngineResult<String>;
    };
}