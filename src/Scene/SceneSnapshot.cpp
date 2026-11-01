#include "Scene/SceneSnapshot.hpp"

#include "Scene/ModelComponent.hpp"

namespace elm {

SceneSnapshot SceneSnapshot::Take(const Scene& scene, const ModelCatalog* models)
{
    SceneSnapshot snapshot;
    snapshot.m_scene = scene.Clone();
    // Models share their mesh data: copies are cheap
    if (models) {
        models->ForEach([&](ModelHandler handle, const Model& model) {
            const Path* file = models->FileOf(handle);
            snapshot.m_models.push_back({ handle, model, file ? *file : Path { } });
        });
    }
    return snapshot;
}

void SceneSnapshot::Restore(Scene& scene, ModelCatalog* models) &&
{
    scene.RestoreFrom(std::move(m_scene));
    if (!models)
        return;

    UnorderedMap<ModelHandler, ModelHandler> reinserted;
    Set<ModelHandler> known;
    for (auto& [handle, model, file] : m_models) {
        if (models->Get(handle)) {
            known.insert(handle);
            continue;
        }
        // Removed meanwhile (a script cleared the scene): back with a new handle and its file
        const auto inserted = models->Insert(std::move(model), std::move(file));
        reinserted.emplace(handle, inserted);
        known.insert(inserted);
    }

    Set<ModelHandler> used;
    for (auto& component : scene.Components<ModelComponent>().Components()) {
        if (const auto found = reinserted.find(component.model); found != reinserted.end())
            component.model = found->second;
        used.insert(component.model);
    }

    Vector<ModelHandler> added;
    models->ForEach([&](ModelHandler handle, const Model&) {
        if (!known.contains(handle) && !used.contains(handle))
            added.push_back(handle);
    });
    for (const auto handle : added)
        models->Remove(handle);
}

} // namespace elm
