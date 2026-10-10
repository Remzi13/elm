#pragma once

#include <filesystem>

#include "core/Error.hpp"
#include "resmgr/Model.hpp"
#include "Scene/TestScenes.hpp"

namespace elm::resmgr {
    class GltfExporter {
    public:
        [[nodiscard]] static auto ExportModel(const Model& model, const std::filesystem::path& path) -> EngineResult<void>;
        [[nodiscard]] static auto Export(const std::filesystem::path& path, const Scene& scene) -> EngineResult<void>;
        [[nodiscard]] static auto LoadModel(const std::filesystem::path& path, size_t meshIndex = 0, size_t primitiveIndex = 0)
            -> EngineResult<Model>;
        [[nodiscard]] static auto LoadScene(Scene& scene, render::RenderResources& resources, const std::filesystem::path& path)
            -> EngineResult<void>;
    };
}