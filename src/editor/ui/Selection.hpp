#pragma once

#include "core/Std.hpp"

#include "Scene/NodeId.hpp"

#include <span>

namespace elm {

    class Scene;

    /// The nodes Scene Hierarchy marks, in the order they were selected; the last is the active node: Inspector
    /// shows it, the viewport's gizmo stands on it. A click in the tree or on the view selects, Ctrl adds. A node that
    /// went finds nothing, like any stale NodeId, until DropRemoved: Editor calls it before every frame of the windows.
    /// Owner: Editor. Thread: update (UI).
    class Selection {
    public:
        /// The last selected node, invalid when nothing is selected.
        [[nodiscard]] NodeId Active() const { return m_nodes.empty() ? NodeId {} : m_nodes.back(); }
        [[nodiscard]] std::span<const NodeId> Nodes() const { return m_nodes; }
        [[nodiscard]] bool Contains(NodeId node) const;

        /// Only this node; an invalid one selects nothing.
        void Select(NodeId node);
        /// Only these nodes, the last one active; invalid ones are left out.
        void Select(std::span<const NodeId> nodes);
        /// Adds the node, or makes it active when it is already selected; an invalid one changes nothing.
        void Add(NodeId node);
        /// The node before it becomes active when the active node goes.
        void Remove(NodeId node);
        void Clear() { m_nodes.clear(); }
        /// Forgets the nodes the scene no longer has: the node selected before them becomes active.
        void DropRemoved(const Scene& scene);

    private:
        Vector<NodeId> m_nodes;
    };

} // namespace elm
