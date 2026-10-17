#include "Scene/SceneSnapshot.hpp"

#include "Scene/ModelComponent.hpp"

namespace elm {

SceneSnapshot SceneSnapshot::Take(const Scene& scene, const graphics::ModelManager* models)
{
    SceneSnapshot snapshot;
    snapshot.m_scene = scene.Clone();
    // Models share their mesh data: copies are cheap
    if (models) {
        models->ForEach([&](graphics::ModelHandler handle, const graphics::Model& model) {
            snapshot.m_models.emplace_back(handle, model);
        });
    }
    return snapshot;
}

void SceneSnapshot::Restore(Scene& scene, graphics::ModelManager* models) &&
{
    scene.RestoreFrom(std::move(m_scene));
    if (!models)
        return;

    UnorderedMap<graphics::ModelHandler, graphics::ModelHandler> reinserted;
    Set<graphics::ModelHandler> known;
    for (auto& [handle, model] : m_models) {
        if (models->Get(handle)) {
            known.insert(handle);
            continue;
        }
        // Removed meanwhile (a script cleared the scene): back with a new handle
        const auto inserted = models->Insert(std::move(model));
        reinserted.emplace(handle, inserted);
        known.insert(inserted);
    }

    Set<graphics::ModelHandler> used;
    for (auto& component : scene.Components<ModelComponent>().Components()) {
        if (const auto found = reinserted.find(component.model); found != reinserted.end())
            component.model = found->second;
        used.insert(component.model);
    }

    Vector<graphics::ModelHandler> added;
    models->ForEach([&](graphics::ModelHandler handle, const graphics::Model&) {
        if (!known.contains(handle) && !used.contains(handle))
            added.push_back(handle);
    });
    for (const auto handle : added)
        models->Remove(handle);
}

} // namespace elm
