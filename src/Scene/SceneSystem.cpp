#include "Scene/SceneSystem.hpp"

#include "model/ModelCatalog.hpp"

#include <utility>

namespace elm {

    SceneSystem::SceneSystem(ModelCatalog& models)
        : m_models(models)
    {
    }

    Vector<NodeId> SceneSystem::Append(SceneAsset asset)
    {
        const auto handlers = m_models.InsertAll(std::move(asset.models), asset.modelFiles);
        return m_scene.Append(std::move(asset), handlers);
    }

    void SceneSystem::UpdateTransforms()
    {
        m_scene.UpdateWorldTransforms();
    }

    SceneSnapshot SceneSystem::TakeSnapshot() const
    {
        return SceneSnapshot::Take(m_scene, &m_models);
    }

    void SceneSystem::Restore(SceneSnapshot snapshot)
    {
        std::move(snapshot).Restore(m_scene, &m_models);
    }

    void SceneSystem::Clear()
    {
        m_scene.Clear();
    }

} // namespace elm
