#include "Scene/ComponentStores.hpp"

namespace elm {

void ComponentStores::Remove(NodeId node)
{
    for (const auto& store : m_stores) {
        if (store)
            store->Remove(node);
    }
}

void ComponentStores::Copy(NodeId from, NodeId to)
{
    for (const auto& store : m_stores) {
        if (store)
            store->Copy(from, to);
    }
}

void ComponentStores::Clear()
{
    for (const auto& store : m_stores) {
        if (store)
            store->Clear();
    }
}

ComponentStores ComponentStores::Clone() const
{
    ComponentStores copy;
    for (size_t type = 0; type < m_stores.size(); ++type) {
        if (m_stores[type])
            copy.m_stores[type] = m_stores[type]->Clone();
    }
    return copy;
}

void ComponentStores::TakeFrom(ComponentStores&& source, const std::function<NodeId(NodeId)>& remap)
{
    for (size_t type = 0; type < m_stores.size(); ++type) {
        auto& from = source.m_stores[type];
        if (!from)
            continue;
        auto& to = m_stores[type];
        if (!to)
            to = from->MakeEmpty();
        to->TakeFrom(*from, remap);
    }
}

} // namespace elm
