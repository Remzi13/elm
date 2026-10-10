#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "resmgr/Model.hpp"
#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class GltfExporter {
    public:
        [[nodiscard]] static auto ExportModel(const Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto Export(const Path& path, const Scene& scene) -> EngineResult<void>;
        [[nodiscard]] static auto LoadModel(const Path& path, size_t meshIndex = 0, size_t primitiveIndex = 0)
            -> EngineResult<Model>;
        [[nodiscard]] static auto LoadScene(Scene& scene, render::RenderResources& resources, const Path& path)
            -> EngineResult<void>;
    };
}
