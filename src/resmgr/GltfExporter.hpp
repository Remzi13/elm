#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/Scene.hpp"
#include "Scene/SceneAsset.hpp"

namespace elm::resmgr {
    class GltfExporter {
    public:
        [[nodiscard]] static auto ExportModel(const graphics::Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto Export(const Path& path, const Scene& scene, const graphics::ModelManager& models) -> EngineResult<void>;
        /// Reads the default glTF scene as one model: every mesh primitive becomes a submesh
        /// with its node transform baked into the geometry.
        [[nodiscard]] static auto LoadModel(const Path& path) -> EngineResult<graphics::Model>;
        /// Reads the default glTF scene: one model per glTF mesh, one scene node per glTF node.
        [[nodiscard]] static auto LoadScene(const Path& path) -> EngineResult<SceneAsset>;
    };
}
