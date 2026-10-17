#pragma once

#include "graphics/Model.hpp"

#include "Scene/SceneAsset.hpp"

#include <span>

namespace elm::graphics {

    enum class SceneImportMode {
        /// Every selected mesh node baked into one model in scene space.
        MergeAll,
        /// One model per glTF mesh in its own space; nodes become instances.
        ModelPerMesh,
        /// Nodes at SceneImportOptions::groupDepth start a model with the selected meshes below them.
        ModelPerGroup
    };

    struct SceneImportOptions {
        SceneImportMode mode { SceneImportMode::MergeAll };
        /// ModelPerGroup: depth of the group nodes, 0 = scene roots.
        uint32_t groupDepth { 0 };
        /// MergeAll: name of the model.
        String name;
    };

    /// A model built from a loaded scene and the scene nodes that place it.
    struct ImportedModel {
        Model model;
        /// Nodes of the asset using this model; SceneAsset::NoNode means "at the scene origin"
        /// (MergeAll bakes everything into scene space).
        Vector<size_t> instanceNodes;
    };

    /// Builds models from the selected nodes of a scene asset (selected: one flag per scene node).
    [[nodiscard]] Vector<ImportedModel> BuildSceneImport(const SceneAsset& asset, const Vector<bool>& selected,
        const SceneImportOptions& options);

    /// Depth of every asset node, 0 for roots.
    [[nodiscard]] Vector<uint32_t> ComputeNodeDepths(std::span<const SceneAsset::Node> nodes);

} // namespace elm::graphics
