#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/ModelManager.hpp"

#include "Scene/ComponentStore.hpp"
#include "Scene/PhysicsComponents.hpp"
#include "Scene/ScriptComponent.hpp"

#include <optional>

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

struct SceneAsset;

class Scene {
public:
    static constexpr size_t NoNode = ~size_t{ 0 };

    struct Node {
        size_t parentIndex { NoNode };
        Matrix4x4 localTransform;
        Matrix4x4 worldTransform;
        String name;
        graphics::ModelHandler model;
        Vector4 color { 1.0f, 1.0f, 1.0f, 1.0f };
        bool visible { true };
        /// Assigned by the scene; nodes pushed into `nodes` directly get one on the first IdOf().
        NodeId id;
    };

    [[nodiscard]] Node& AddNode(graphics::ModelHandler model, String name = { });
    /// Appends a copy of a node under a new id.
    Node& AppendNode(Node node);
    [[nodiscard]] bool SetWorldTransform(size_t nodeIndex, const Matrix4x4& transform);
    /// Sets the transform relative to the parent, world transforms follow on UpdateWorldTransforms().
    bool SetLocalTransform(size_t nodeIndex, const Matrix4x4& transform);
    /// Recomputes world transforms after SetLocalTransform calls; parents may follow their children.
    void UpdateWorldTransforms();
    /// Removes a node, its children move to its parent and keep their world transforms.
    bool RemoveNode(size_t nodeIndex);
    /// Uploads the asset models into models and appends its nodes with their components. Returns the
    /// index of the first appended node.
    size_t Append(SceneAsset&& asset, graphics::ModelManager& models);
    void Clear();
    void ReserveNodes(size_t capacity);

    /// A copy with the same nodes, node ids and components (Play mode keeps one to come back to).
    [[nodiscard]] Scene Clone() const;
    /// Becomes the snapshot again, node ids included. Ids handed out since the snapshot was taken
    /// stay stale: their slots go on from the generation they reached.
    void RestoreFrom(Scene&& snapshot);

    /// Id of the node at nodeIndex, an invalid id for an index out of range.
    [[nodiscard]] NodeId IdOf(size_t nodeIndex);
    /// Index of a live node, NoNode for a removed or foreign id.
    [[nodiscard]] size_t IndexOf(NodeId id) const;
    [[nodiscard]] Node* Find(NodeId id);
    [[nodiscard]] const Node* Find(NodeId id) const;

    [[nodiscard]] size_t NodeCount() const noexcept { return nodes.size(); }
    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }

    /// Components of type T keyed by node id, the store is created on first use. A node's
    /// components go away with it (RemoveNode, Clear); copies of nodes do not take them along.
    template <typename T>
    [[nodiscard]] ComponentStore<T>& Components()
    {
        const uint32_t type = ComponentType<T>();
        if (type >= m_componentStores.size())
            m_componentStores.resize(type + 1);
        auto& store = m_componentStores[type];
        if (!store)
            store = MakeUnique<ComponentStore<T>>();
        return static_cast<ComponentStore<T>&>(*store);
    }
    /// Gives node `to` copies of the components of node `from`.
    void CopyComponents(NodeId from, NodeId to);
    /// nullptr while no component of type T was ever added.
    template <typename T>
    [[nodiscard]] const ComponentStore<T>* FindComponents() const noexcept
    {
        const uint32_t type = ComponentType<T>();
        return type < m_componentStores.size() ? static_cast<const ComponentStore<T>*>(m_componentStores[type].get()) : nullptr;
    }

    Vector<Node> nodes;

private:
    struct IdSlot {
        /// Last known node index, repaired by IndexOf when nodes were moved behind the scene's back.
        mutable size_t index { NoNode };
        uint8_t generation { 0 };
    };

    NodeId AllocateId(size_t nodeIndex);
    void FreeId(NodeId id);

    Vector<IdSlot> m_idSlots;
    Vector<uint32_t> m_freeIdSlots;
    /// Indexed by ComponentType<T>(), empty for types this scene never saw.
    Vector<UniquePtr<IComponentStore>> m_componentStores;
    bool m_transformsDirty { false };
};

/// Components of a node as a scene file has them, before the node gets its id in a scene.
struct NodeComponents {
    std::optional<ScriptComponent> script;
    std::optional<RigidBody> rigidBody;
    std::optional<Collider> collider;
};

/// Scene as read from a file: models are CPU-side only and not owned by any ModelManager yet.
struct SceneAsset {
    static constexpr size_t NoModel = ~size_t{ 0 };

    /// Node models are left empty, nodeModels tells which model each node uses.
    Scene scene;
    Vector<graphics::Model> models;
    /// Per scene node: index into models or NoModel.
    Vector<size_t> nodeModels;
    /// Per scene node, may be shorter than the nodes. The nodes have no ids yet, so an Entity prop
    /// of a script holds NodeRef(node index) instead; Scene::Append turns it into the node's id.
    Vector<NodeComponents> nodeComponents;

    [[nodiscard]] static NodeId NodeRef(size_t nodeIndex) noexcept { return NodeId::FromRaw(static_cast<uint32_t>(nodeIndex + 1)); }
    /// Node index of a NodeRef, ~0 for an empty reference.
    [[nodiscard]] static size_t NodeIndexOf(NodeId reference) noexcept { return reference ? reference.Raw() - 1 : ~size_t { 0 }; }
};

class TestScenes {
public:
    static void BuildScene(ScenePreset preset, uint32_t targetInstanceCount, Scene& scene,
        graphics::ModelManager& models);

private:
    static void BuildWallAndGrid(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildRooms(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildPhysicsSandbox(uint32_t count, Scene& scene, graphics::ModelManager& models);
    static void BuildBox(uint32_t count, Scene& scene, graphics::ModelManager& models);
};

} // namespace Engine
