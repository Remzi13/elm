#pragma once

#include "core/Error.hpp"
#include "core/FileSystem.hpp"

#include "model/ModelCatalog.hpp"

#include "Scene/Scene.hpp"
#include "Scene/SceneAsset.hpp"

namespace elm::resource {
    class GltfExporter {
    public:
        [[nodiscard]] static auto ExportModel(const Model& model, const Path& path) -> EngineResult<void>;
        [[nodiscard]] static auto Export(const Path& path, const Scene& scene, const ModelCatalog& models) -> EngineResult<void>;
        /// Reads the default glTF scene as one model: every mesh primitive becomes a submesh
        /// with its node transform baked into the geometry.
        [[nodiscard]] static auto LoadModel(const Path& path) -> EngineResult<Model>;
        /// Reads the default glTF scene: one model per glTF mesh, one scene node per glTF node.
        [[nodiscard]] static auto LoadScene(const Path& path) -> EngineResult<SceneAsset>;
    };
}
