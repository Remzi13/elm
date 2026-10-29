#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/ModelHandler.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/ComponentStores.hpp"
#include "Scene/ComponentTypes.hpp"
#include "Scene/NodeId.hpp"
#include "Scene/NodeIdTable.hpp"

#include <span>

namespace elm {

struct SceneAsset;

/// Nodes of a scene and their components. The nodes are a flat array, a parent always comes before its
/// children. Owners: EngineApp (the live scene) and SceneSnapshot. Update thread only.
/// Nodes are handed out read-only: every change goes through the scene, so it keeps node ids, parents,
/// the order and components in step. A stale or foreign NodeId finds nothing: Find returns nullptr,
/// setters false. World transforms are derived from the local ones, parallel to the nodes, and current
/// unless a local change is pending; UpdateWorldTransforms() catches up.
class Scene {
public:
    static constexpr size_t NoNode = NodeIdTable::NoIndex;

    struct Node {
        NodeId id;
        NodeId parent;
        String name;
        math::Matrix4x4 localTransform;
    };

    enum class SetParentResult : uint8_t {
        Done,
        StaleNode,
        Cycle,
        SingularParent,
    };

    [[nodiscard]] NodeId AddNode(const String& name, const math::Matrix4x4& localTransform = math::Matrix4x4::Identity(), NodeId parent = { });    
    bool RemoveNode(NodeId node);

    [[nodiscard]] SetParentResult SetParent(NodeId node, NodeId parent);    
    bool SetLocalTransform(NodeId node, const math::Matrix4x4& transform);    
    [[nodiscard]] bool SetWorldTransform(NodeId node, const math::Matrix4x4& transform);
    bool SetName(NodeId node, String name);

    void UpdateWorldTransforms();

    [[nodiscard]] Vector<NodeId> Append(SceneAsset&& asset, std::span<const graphics::ModelHandler> models);
    void Clear();
    void ReserveNodes(size_t capacity);

    /// A copy with the same nodes, node ids and components (Play mode keeps one to come back to).
    [[nodiscard]] Scene Clone() const;
    /// Becomes the snapshot again, node ids included. Ids handed out since the snapshot was taken
    /// stay stale.
    void RestoreFrom(Scene&& snapshot);

    /// In scene order, parents before children; indices into it are valid for one operation.
    [[nodiscard]] std::span<const Node> Nodes() const noexcept { return m_nodes; }
    /// World transform of every node, parallel to Nodes().
    [[nodiscard]] std::span<const math::Matrix4x4> WorldTransforms() const noexcept { return m_worldTransforms; }
    [[nodiscard]] const Node* Find(NodeId id) const;
    /// Index of a live node, NoNode for a removed or foreign id.
    [[nodiscard]] size_t IndexOf(NodeId id) const { return m_ids.IndexOf(id); }
    /// Id of the node at nodeIndex, an invalid id for an index out of range.
    [[nodiscard]] NodeId IdOf(size_t nodeIndex) const noexcept { return nodeIndex < m_nodes.size() ? m_nodes[nodeIndex].id : NodeId { }; }
    [[nodiscard]] size_t NodeCount() const noexcept { return m_nodes.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_nodes.empty(); }

    /// Components of type T keyed by node id, the store is created on first use. A node's
    /// components go away with it (RemoveNode, Clear); copies of nodes do not take them along.
    template <SceneComponent T>
    [[nodiscard]] ComponentStore<T>& Components() { return m_components.Components<T>(); }
    /// Gives node `to` copies of the components of node `from`.
    void CopyComponents(NodeId from, NodeId to);
    /// nullptr while no component of type T was ever added.
    template <SceneComponent T>
    [[nodiscard]] const ComponentStore<T>* FindComponents() const noexcept { return m_components.FindComponents<T>(); }

private:
    [[nodiscard]] Node* MutableNode(NodeId id);
    NodeId PushNode(Node node, const math::Matrix4x4& worldTransform);
    /// ids[i] is the id asset node i got.
    void AppendComponents(SceneAsset& asset, std::span<const NodeId> ids, std::span<const graphics::ModelHandler> models);
    /// Moves the node and its descendants in [nodeIndex, parentIndex] after the other nodes there, so the
    /// node can become a child of the node at parentIndex, which is not one of its descendants.
    void MoveSubtreeAfter(size_t nodeIndex, size_t parentIndex);
    /// True when the node at nodeIndex is the node at candidateIndex or one of its ancestors.
    [[nodiscard]] bool IsAncestorOrSelf(size_t nodeIndex, size_t candidateIndex) const;
    /// Children of the removed node go to its parent, nodes after it move one index down.
    void CloseGap(size_t removedIndex, const Node& removed);

    Vector<Node> m_nodes;
    Vector<math::Matrix4x4> m_worldTransforms;
    NodeIdTable m_ids;
    ComponentStores m_components;
    bool m_transformsDirty { false };
};

} // namespace elm
