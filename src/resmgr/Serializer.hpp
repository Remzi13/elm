#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class Serializer {
    public:
        [[nodiscard]] static auto SaveModel(const graphics::Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto SaveScene(const Scene& scene, const graphics::ModelManager& models, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto LoadScene(Scene& scene, graphics::ModelManager& models,
            render::RenderResources& resources, const Path& path) -> EngineResult<void>;

    private:
        [[nodiscard]] static auto WriteFile(const Path& path, StringView contents) -> EngineResult<void>;
        [[nodiscard]] static auto ReadFile(const Path& path) -> EngineResult<String>;
    };
}