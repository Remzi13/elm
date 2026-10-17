#pragma once

#include "core/Std.hpp"

#include "math/Matrix.hpp"

#include "graphics/Model.hpp"
#include "graphics/ModelHandler.hpp"

#include "Scene/ComponentStores.hpp"
#include "Scene/NodeId.hpp"

namespace elm {

/// Scene as read from a file, before Scene::Append: models are CPU-side only and not owned by any
/// ModelManager yet, nodes have no ids. May be built on any thread; Scene::Append runs on update.
struct SceneAsset {
    static constexpr size_t NoNode = ~size_t { 0 };
    static constexpr size_t NoModel = ~size_t { 0 };

    /// A node as the file describes it: its parent is an index in this asset.
    struct Node {
        /// NoNode for a root; may come after the node itself.
        size_t parentIndex { NoNode };
        math::Matrix4x4 localTransform;
        String name;
    };

    Vector<Node> nodes;
    Vector<graphics::Model> models;
    /// The nodes have no ids yet: components are keyed by NodeRef(node index), and so is an Entity prop
    /// of a script. The model of a ModelComponent is ModelRef(model index). Scene::Append turns them all
    /// into the ids the nodes get and the handles the models got.
    ComponentStores components;

    [[nodiscard]] static NodeId NodeRef(size_t nodeIndex) noexcept { return NodeId::FromRaw(static_cast<uint32_t>(nodeIndex + 1)); }
    /// Node index of a NodeRef, NoNode for an empty reference.
    [[nodiscard]] static size_t NodeIndexOf(NodeId reference) noexcept { return reference ? reference.Raw() - 1 : NoNode; }
    [[nodiscard]] static graphics::ModelHandler ModelRef(size_t modelIndex) noexcept { return graphics::ModelHandler::FromRaw(static_cast<uint32_t>(modelIndex + 1)); }
    /// Model index of a ModelRef, NoModel for an empty reference.
    [[nodiscard]] static size_t ModelIndexOf(graphics::ModelHandler reference) noexcept { return reference ? reference.Raw() - 1 : NoModel; }

    /// Model index of a node's ModelComponent, NoModel for a node without one or without a model.
    [[nodiscard]] size_t ModelIndexOfNode(size_t nodeIndex) const;
    /// World transform of every node, from the local ones; a parent cycle makes a node a root.
    [[nodiscard]] Vector<math::Matrix4x4> WorldTransforms() const;
};

} // namespace elm
