#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "resmgr/ModelManager.hpp"
#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class GltfExporter {
    public:
        [[nodiscard]] static auto ExportModel(const Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto Export(const Path& path, const Scene& scene, const ModelManager& models) -> EngineResult<void>;
        [[nodiscard]] static auto LoadModel(const Path& path, size_t meshIndex = 0,
            size_t primitiveIndex = static_cast<size_t>(-1))
            -> EngineResult<Model>;
        [[nodiscard]] static auto LoadScene(Scene& scene, ModelManager& models,
            render::RenderResources& resources, const Path& path)
            -> EngineResult<void>;
    };
}
