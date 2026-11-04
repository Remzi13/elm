#include "graphics/ModelMeshes.hpp"

#include <algorithm>

namespace elm::graphics {

    ModelMeshes::ModelMeshes(render::RenderResources& resources)
        : m_resources(&resources)
    {
    }

    void ModelMeshes::Sync(const ModelCatalog& catalog)
    {
        // Entries whose model left the catalog (stale handle) lose their meshes
        for (Entry& entry : m_entries) {
            if (entry.handler.IsValid() && !catalog.Get(entry.handler))
                entry = {};
        }
        catalog.ForEach([&](ModelHandler handler, const Model& model) {
            const size_t index = handler.Index() - 1;
            if (index >= m_entries.size())
                m_entries.resize(index + 1);
            Entry& entry = m_entries[index];
            const uint32_t revision = catalog.RevisionOf(handler);
            if (entry.handler == handler && entry.revision == revision)
                return;
            // A replaced model keeps the meshes whose geometry it still shares
            Vector<render::Mesh> previous = entry.handler == handler ? std::move(entry.meshes) : Vector<render::Mesh> {};
            entry.meshes = CreateMeshes(model, std::move(previous));
            entry.handler = handler;
            entry.revision = revision;
        });
    }

    std::span<const render::Mesh> ModelMeshes::GetMeshes(ModelHandler handler) const noexcept
    {
        const Entry* entry = Find(handler);
        return entry ? std::span<const render::Mesh>(entry->meshes) : std::span<const render::Mesh>();
    }

    void ModelMeshes::Clear()
    {
        m_entries.clear();
    }

    const ModelMeshes::Entry* ModelMeshes::Find(ModelHandler handler) const noexcept
    {
        if (!handler.IsValid() || handler.Index() == 0 || handler.Index() > m_entries.size())
            return nullptr;
        const Entry& entry = m_entries[handler.Index() - 1];
        return entry.handler == handler ? &entry : nullptr;
    }

    Vector<render::Mesh> ModelMeshes::CreateMeshes(const Model& model, Vector<render::Mesh> previous)
    {
        Vector<render::Mesh> meshes;
        meshes.reserve(model.submeshes.size());
        for (const auto& submesh : model.submeshes) {
            const auto reused = std::find_if(previous.begin(), previous.end(), [&](const render::Mesh& mesh) {
                return mesh.IsValid() && mesh.GetData() == submesh.data;
            });
            if (reused != previous.end())
                meshes.push_back(std::move(*reused));
            else if (submesh.data)
                meshes.emplace_back(*m_resources, submesh.data);
            else
                meshes.emplace_back();
        }
        return meshes;
    }

} // namespace elm::graphics
