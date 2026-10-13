#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/ModelManager.hpp"

namespace elm {

using namespace math;

enum class ScenePreset {
    Box,
    WallAndGrid,
    RoomsAndCorridors,
    PhysicsSandbox
};

struct SceneAsset;

struct NodeTag;
/// Stable reference to a scene node: survives removal and reordering of other nodes,
/// a removed node's id stops resolving (generation check).
using NodeId = core::Handle<NodeTag>;

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
    /// Uploads the asset models into models and appends its nodes. Returns the index of the first appended node.
    size_t Append(SceneAsset&& asset, graphics::ModelManager& models);
    void Clear();
    void ReserveNodes(size_t capacity);

    /// Id of the node at nodeIndex, an invalid id for an index out of range.
    [[nodiscard]] NodeId IdOf(size_t nodeIndex);
    /// Index of a live node, NoNode for a removed or foreign id.
    [[nodiscard]] size_t IndexOf(NodeId id) const;
    [[nodiscard]] Node* Find(NodeId id);
    [[nodiscard]] const Node* Find(NodeId id) const;

    [[nodiscard]] size_t NodeCount() const noexcept { return nodes.size(); }
    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }

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
    bool m_transformsDirty { false };
};

/// Scene as read from a file: models are CPU-side only and not owned by any ModelManager yet.
struct SceneAsset {
    static constexpr size_t NoModel = ~size_t{ 0 };

    /// Node models are left empty, nodeModels tells which model each node uses.
    Scene scene;
    Vector<graphics::Model> models;
    /// Per scene node: index into models or NoModel.
    Vector<size_t> nodeModels;
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
