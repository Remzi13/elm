#pragma once

#include "model/ModelCatalog.hpp"

#include "render/api/RenderResources.hpp"

#include <span>

namespace elm::graphics {

    /// The GPU meshes of the models of a ModelCatalog, one per submesh. Sync makes the meshes of new
    /// models, makes them again for replaced ones (keeping meshes whose geometry stayed) and drops those
    /// of removed models. A stale handle has no meshes. Owner: the drawing side of the scene (Engine, the
    /// Model Editor preview), thread: update; destroyed before RenderSystem::Shutdown.
    class ModelMeshes {
    public:
        explicit ModelMeshes(render::RenderResources& resources);

        ModelMeshes(const ModelMeshes&) = delete;
        ModelMeshes& operator=(const ModelMeshes&) = delete;
        ModelMeshes(ModelMeshes&&) noexcept = default;
        ModelMeshes& operator=(ModelMeshes&&) noexcept = default;

        /// Before drawing: the meshes follow the catalog as it is now.
        void Sync(const ModelCatalog& catalog);
        /// Meshes of a model as of the last Sync, in the order of its submeshes.
        [[nodiscard]] std::span<const render::Mesh> GetMeshes(ModelHandler handler) const noexcept;
        void Clear();

        /// Calls fn(const Model::Submesh&, render::MeshHandle) for every submesh of the model that has a mesh.
        template <typename Fn>
        void ForEachSubmesh(const ModelCatalog& catalog, ModelHandler handler, Fn&& fn) const
        {
            const Model* model = catalog.Get(handler);
            const auto meshes = GetMeshes(handler);
            if (!model || meshes.size() != model->submeshes.size())
                return;
            for (size_t index = 0; index < meshes.size(); ++index)
                fn(model->submeshes[index], meshes[index].GetHandle());
        }

    private:
        struct Entry {
            ModelHandler handler;
            uint32_t revision { 0 };
            Vector<render::Mesh> meshes;
        };

        [[nodiscard]] const Entry* Find(ModelHandler handler) const noexcept;
        [[nodiscard]] Vector<render::Mesh> CreateMeshes(const Model& model, Vector<render::Mesh> previous);

        render::RenderResources* m_resources;
        /// By handle index - 1, like the catalog's slots.
        Vector<Entry> m_entries;
    };

} // namespace elm::graphics
