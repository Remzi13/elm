#include "editor/ui/Selection.hpp"

#include "Scene/Scene.hpp"

#include <algorithm>

namespace elm {

    bool Selection::Contains(NodeId node) const
    {
        return std::find(m_nodes.begin(), m_nodes.end(), node) != m_nodes.end();
    }

    void Selection::Select(NodeId node)
    {
        m_nodes.clear();
        Add(node);
    }

    void Selection::Select(std::span<const NodeId> nodes)
    {
        m_nodes.clear();
        for (const NodeId node : nodes)
            Add(node);
    }

    void Selection::Add(NodeId node)
    {
        if (!node)
            return;
        Remove(node);
        m_nodes.push_back(node);
    }

    void Selection::Remove(NodeId node)
    {
        std::erase(m_nodes, node);
    }

    void Selection::DropRemoved(const Scene& scene)
    {
        std::erase_if(m_nodes, [&](NodeId node) { return !scene.Find(node); });
    }

} // namespace elm
