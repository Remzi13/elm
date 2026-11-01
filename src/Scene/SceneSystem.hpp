#pragma once

#include "Scene/Scene.hpp"
#include "Scene/SceneAsset.hpp"
#include "Scene/SceneSnapshot.hpp"

namespace elm {

    class ModelCatalog;

    /// The scene of the program: nodes, components, world transforms (docs/core/Системы.md). The models its
    /// nodes draw live in the resources' ModelCatalog: a scene put in adds its models there, a snapshot keeps
    /// and restores them with the nodes. Readers take the scene const; writers (scripts, Play, the editor's
    /// edits) are given a non-const SceneSystem&. Owner: the program (Game, Editor), thread: update.
    class SceneSystem {
    public:
        explicit SceneSystem(ModelCatalog& models);

        SceneSystem(const SceneSystem&) = delete;
        SceneSystem& operator=(const SceneSystem&) = delete;
        SceneSystem(SceneSystem&&) = delete;
        SceneSystem& operator=(SceneSystem&&) = delete;

        [[nodiscard]] const Scene& GetScene() const noexcept { return m_scene; }
        /// For the writers: scripts, Play, edits.
        [[nodiscard]] Scene& EditScene() noexcept { return m_scene; }

        /// Puts a scene read from a file after the nodes there are: its models go into the catalog.
        /// Returns the ids of its nodes in the asset's order.
        [[nodiscard]] Vector<NodeId> Append(SceneAsset asset);
        /// World transforms from the local ones, after everything of the frame moved nodes.
        void UpdateTransforms();
        /// The scene and its models as they are now, to come back to (Edit mode around Play).
        [[nodiscard]] SceneSnapshot TakeSnapshot() const;
        /// The snapshot's scene with its node ids; its models are back in the catalog.
        void Restore(SceneSnapshot snapshot);
        /// Removes every node; the models stay in the catalog (they are the resources').
        void Clear();

    private:
        ModelCatalog& m_models;
        Scene m_scene;
    };

} // namespace elm
