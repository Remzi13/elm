#include "Scene/SceneAsset.hpp"

#include "Scene/ModelComponent.hpp"

namespace elm {

size_t SceneAsset::ModelIndexOfNode(size_t nodeIndex) const
{
    const auto* store = components.FindComponents<ModelComponent>();
    const ModelComponent* model = store ? store->Find(NodeRef(nodeIndex)) : nullptr;
    return model ? ModelIndexOf(model->model) : NoModel;
}

Vector<math::Matrix4x4> SceneAsset::WorldTransforms() const
{
    // A parent may come after its child: walk up to a computed ancestor or a root, then compute the
    // chain top down
    enum : uint8_t { Unvisited, Visiting, Done };
    Vector<uint8_t> state(nodes.size(), Unvisited);
    Vector<math::Matrix4x4> world(nodes.size());
    Vector<size_t> chain;
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        chain.clear();
        for (size_t current = nodeIndex; current < nodes.size() && state[current] == Unvisited; current = nodes[current].parentIndex) {
            state[current] = Visiting;
            chain.push_back(current);
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Node& node = nodes[*it];
            const size_t parent = node.parentIndex;
            world[*it] = parent < nodes.size() && state[parent] == Done ? world[parent] * node.localTransform : node.localTransform;
            state[*it] = Done;
        }
    }
    return world;
}

} // namespace elm
