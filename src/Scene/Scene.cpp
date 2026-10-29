#include "Scene/Scene.hpp"

#include "core/Debug.hpp"

#include "math/Rotation.hpp"

#include "Scene/ModelComponent.hpp"
#include "Scene/SceneAsset.hpp"
#include "Scene/ScriptComponent.hpp"

#include <initializer_list>
#include <variant>

namespace elm {

namespace {

    /// Asset nodes in an order with every parent before its children, the asset's order where it allows.
    /// A node goes in after those of its ancestors that are not in yet; a parent cycle is cut where it closes.
    Vector<size_t> ParentsFirst(const SceneAsset& asset)
    {
        const auto& nodes = asset.nodes;
        Vector<bool> placed(nodes.size(), false);
        Vector<size_t> order;
        order.reserve(nodes.size());
        Vector<size_t> chain;
        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            chain.clear();
            for (size_t current = nodeIndex; current < nodes.size() && !placed[current]; current = nodes[current].parentIndex) {
                placed[current] = true;
                chain.push_back(current);
            }
            order.insert(order.end(), chain.rbegin(), chain.rend());
        }
        return order;
    }

} // namespace

NodeId Scene::AddNode(const String& name, const math::Matrix4x4& localTransform, NodeId parent)
{
    ELM_ASSERT_THREAD(Update);
    const size_t parentIndex = parent ? IndexOf(parent) : NoNode;
    if (parent && parentIndex == NoNode)
        return { };

    const math::Matrix4x4 world = parentIndex == NoNode ? localTransform : m_worldTransforms[parentIndex] * localTransform;
    return PushNode({ .parent = parent, .name = name, .localTransform = localTransform }, world);
}

bool Scene::RemoveNode(NodeId node)
{
    ELM_ASSERT_THREAD(Update);
    const size_t nodeIndex = IndexOf(node);
    if (nodeIndex == NoNode)
        return false;

    m_components.Remove(node);
    m_ids.Free(node);
    const Node removed = std::move(m_nodes[nodeIndex]);
    m_nodes.erase(m_nodes.begin() + static_cast<std::ptrdiff_t>(nodeIndex));
    m_worldTransforms.erase(m_worldTransforms.begin() + static_cast<std::ptrdiff_t>(nodeIndex));
    CloseGap(nodeIndex, removed);
    return true;
}

Scene::SetParentResult Scene::SetParent(NodeId node, NodeId parent)
{
    ELM_ASSERT_THREAD(Update);
    const size_t nodeIndex = IndexOf(node);
    const size_t parentIndex = parent ? IndexOf(parent) : NoNode;
    if (nodeIndex == NoNode || (parent && parentIndex == NoNode))
        return SetParentResult::StaleNode;

    UpdateWorldTransforms();
    const math::Matrix4x4 world = m_worldTransforms[nodeIndex];
    if (parentIndex == NoNode) {
        Node& target = m_nodes[nodeIndex];
        target.parent = { };
        target.localTransform = world;
        return SetParentResult::Done;
    }

    if (IsAncestorOrSelf(nodeIndex, parentIndex))
        return SetParentResult::Cycle;
    const auto parentInverse = math::Inverse(m_worldTransforms[parentIndex]);
    if (!parentInverse)
        return SetParentResult::SingularParent;
    // A parent comes before its children
    if (parentIndex > nodeIndex)
        MoveSubtreeAfter(nodeIndex, parentIndex);
    Node& target = m_nodes[IndexOf(node)];
    target.parent = parent;
    target.localTransform = *parentInverse * world;
    return SetParentResult::Done;
}

bool Scene::SetLocalTransform(NodeId node, const math::Matrix4x4& transform)
{
    ELM_ASSERT_THREAD(Update);
    Node* target = MutableNode(node);
    if (!target)
        return false;
    target->localTransform = transform;
    m_transformsDirty = true;
    return true;
}

bool Scene::SetWorldTransform(NodeId node, const math::Matrix4x4& transform)
{
    ELM_ASSERT_THREAD(Update);
    const size_t nodeIndex = IndexOf(node);
    if (nodeIndex == NoNode)
        return false;

    UpdateWorldTransforms();
    Node& target = m_nodes[nodeIndex];
    const size_t parentIndex = IndexOf(target.parent);
    if (parentIndex == NoNode) {
        target.localTransform = transform;
    } else {
        const auto parentInverse = math::Inverse(m_worldTransforms[parentIndex]);
        if (!parentInverse)
            return false;
        target.localTransform = *parentInverse * transform;
    }
    m_worldTransforms[nodeIndex] = transform;

    m_transformsDirty = true;
    UpdateWorldTransforms();
    return true;
}

bool Scene::SetName(NodeId node, String name)
{
    ELM_ASSERT_THREAD(Update);
    Node* target = MutableNode(node);
    if (!target)
        return false;
    target->name = std::move(name);
    return true;
}

void Scene::UpdateWorldTransforms()
{
    ELM_ASSERT_THREAD(Update);
    if (!m_transformsDirty)
        return;
    m_transformsDirty = false;

    for (size_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex) {
        const Node& node = m_nodes[nodeIndex];
        const size_t parentIndex = IndexOf(node.parent);
        ELM_ASSERT((parentIndex == NoNode || parentIndex < nodeIndex) && "a parent comes before its children");
        m_worldTransforms[nodeIndex] = parentIndex < nodeIndex ? m_worldTransforms[parentIndex] * node.localTransform : node.localTransform;
    }
}

Vector<NodeId> Scene::Append(SceneAsset&& asset, std::span<const graphics::ModelHandler> models)
{
    ELM_ASSERT_THREAD(Update);

    Vector<NodeId> ids(asset.nodes.size());
    ReserveNodes(m_nodes.size() + asset.nodes.size());
    
    for (const size_t assetIndex : ParentsFirst(asset)) {
        SceneAsset::Node& source = asset.nodes[assetIndex];
        const NodeId parent = source.parentIndex < ids.size() ? ids[source.parentIndex] : NodeId { };
        ids[assetIndex] = AddNode(source.name, source.localTransform,  parent );
    }
    AppendComponents(asset, ids, models);
    return ids;
}

void Scene::Clear()
{
    ELM_ASSERT_THREAD(Update);
    for (const auto& node : m_nodes)
        m_ids.Free(node.id);
    m_nodes.clear();
    m_worldTransforms.clear();
    m_components.Clear();
    m_transformsDirty = false;
}

void Scene::ReserveNodes(size_t capacity)
{
    m_nodes.reserve(capacity);
    m_worldTransforms.reserve(capacity);
}

void Scene::CopyComponents(NodeId from, NodeId to)
{
    m_components.Copy(from, to);
}

Scene Scene::Clone() const
{
    Scene copy;
    copy.m_nodes = m_nodes;
    copy.m_worldTransforms = m_worldTransforms;
    copy.m_ids = m_ids;
    copy.m_components = m_components.Clone();
    copy.m_transformsDirty = m_transformsDirty;
    return copy;
}

void Scene::RestoreFrom(Scene&& snapshot)
{
    ELM_ASSERT_THREAD(Update);
    m_ids.RestoreFrom(std::move(snapshot.m_ids));
    m_nodes = std::move(snapshot.m_nodes);
    m_worldTransforms = std::move(snapshot.m_worldTransforms);
    // The stores of the snapshot take over: references to the current ones end here
    m_components = std::move(snapshot.m_components);
    m_transformsDirty = true;
}

const Scene::Node* Scene::Find(NodeId id) const
{
    const size_t nodeIndex = IndexOf(id);
    return nodeIndex == NoNode ? nullptr : &m_nodes[nodeIndex];
}

Scene::Node* Scene::MutableNode(NodeId id)
{
    const size_t nodeIndex = IndexOf(id);
    return nodeIndex == NoNode ? nullptr : &m_nodes[nodeIndex];
}

NodeId Scene::PushNode(Node node, const math::Matrix4x4& worldTransform)
{
    node.id = m_ids.Allocate(m_nodes.size());
    m_nodes.push_back(std::move(node));
    m_worldTransforms.push_back(worldTransform);
    return m_nodes.back().id;
}

void Scene::AppendComponents(SceneAsset& asset, std::span<const NodeId> ids, std::span<const graphics::ModelHandler> models)
{
    // The asset keys its components and Entity props by its own nodes and refers to its models by index:
    // they get the ids those nodes have now and the handles those models got
    auto idOf = [&](NodeId reference) {
        const size_t nodeIndex = SceneAsset::NodeIndexOf(reference);
        return nodeIndex < ids.size() ? ids[nodeIndex] : NodeId { };
    };
    for (auto& script : asset.components.Components<ScriptComponent>().Components()) {
        for (auto& prop : script.props) {
            if (auto* reference = std::get_if<NodeId>(&prop.second))
                *reference = idOf(*reference);
        }
    }
    for (auto& model : asset.components.Components<ModelComponent>().Components()) {
        const size_t modelIndex = SceneAsset::ModelIndexOf(model.model);
        model.model = modelIndex < models.size() ? models[modelIndex] : graphics::ModelHandler { };
    }
    m_components.TakeFrom(std::move(asset.components), idOf);
}

void Scene::MoveSubtreeAfter(size_t nodeIndex, size_t parentIndex)
{
    // A stable partition of [nodeIndex, parentIndex]: the other nodes keep their order and come first,
    // the subtree keeps its order and follows them. Parents stay before children on both sides, and
    // nodes outside the range do not move.
    const size_t count = parentIndex - nodeIndex + 1;
    Vector<bool> inSubtree(count, false);
    inSubtree[0] = true;
    for (size_t offset = 1; offset < count; ++offset) {
        const size_t parent = IndexOf(m_nodes[nodeIndex + offset].parent);
        inSubtree[offset] = parent != NoNode && parent >= nodeIndex && inSubtree[parent - nodeIndex];
    }

    Vector<Node> nodes;
    Vector<math::Matrix4x4> worldTransforms;
    nodes.reserve(count);
    worldTransforms.reserve(count);
    for (const bool subtree : { false, true }) {
        for (size_t offset = 0; offset < count; ++offset) {
            if (inSubtree[offset] != subtree)
                continue;
            nodes.push_back(std::move(m_nodes[nodeIndex + offset]));
            worldTransforms.push_back(m_worldTransforms[nodeIndex + offset]);
        }
    }
    for (size_t offset = 0; offset < count; ++offset) {
        m_nodes[nodeIndex + offset] = std::move(nodes[offset]);
        m_worldTransforms[nodeIndex + offset] = worldTransforms[offset];
        m_ids.Move(m_nodes[nodeIndex + offset].id, nodeIndex + offset);
    }
}

bool Scene::IsAncestorOrSelf(size_t nodeIndex, size_t candidateIndex) const
{
    // Ancestors come before their descendants: past nodeIndex the walk cannot meet it any more
    size_t current = candidateIndex;
    while (current != NoNode && current > nodeIndex)
        current = IndexOf(m_nodes[current].parent);
    return current == nodeIndex;
}

void Scene::CloseGap(size_t removedIndex, const Node& removed)
{
    // Children come after their parent: only the nodes after the removed one change
    for (size_t nodeIndex = removedIndex; nodeIndex < m_nodes.size(); ++nodeIndex) {
        Node& node = m_nodes[nodeIndex];
        if (node.parent == removed.id) {
            // world = parentWorld * removedLocal * childLocal stays the same under the new parent
            node.parent = removed.parent;
            node.localTransform = removed.localTransform * node.localTransform;
        }
        m_ids.Move(node.id, nodeIndex);
    }
}

} // namespace elm
