#include "Scene/NodeIdTable.hpp"

#include "core/Debug.hpp"

#include <algorithm>
#include <limits>

namespace elm {

namespace {

    /// Generations a NodeId can tell apart (8 bits of it).
    constexpr uint32_t GenerationCount = uint32_t { std::numeric_limits<uint8_t>::max() } + 1;

} // namespace

NodeId NodeIdTable::Allocate(size_t nodeIndex)
{
    uint32_t slotIndex = 0;
    if (!m_freeSlots.empty()) {
        slotIndex = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        ELM_ASSERT(m_slots.size() < NodeId::MaxIndex && "a scene holds at most NodeId::MaxIndex - 1 nodes");
        slotIndex = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    Slot& slot = m_slots[slotIndex];
    slot.index = nodeIndex;
    slot.unusedGeneration = (std::max)(slot.unusedGeneration, uint32_t { slot.generation } + 1);
    // Index 0 is the invalid id, so ids count slots from 1
    return NodeId(slotIndex + 1, slot.generation);
}

bool NodeIdTable::Free(NodeId id)
{
    if (!LiveSlot(id))
        return false;
    const uint32_t slotIndex = id.Index() - 1;
    Slot& slot = m_slots[slotIndex];
    slot.index = NoIndex;
    // A slot that gave out every generation is retired, so an old id never finds a new node
    if (slot.unusedGeneration >= GenerationCount)
        return true;
    slot.generation = static_cast<uint8_t>(slot.unusedGeneration);
    m_freeSlots.push_back(slotIndex);
    return true;
}

void NodeIdTable::Move(NodeId id, size_t nodeIndex)
{
    const bool live = LiveSlot(id) != nullptr;
    ELM_ASSERT(live && "only the node of a live id moves");
    if (live)
        m_slots[id.Index() - 1].index = nodeIndex;
}

size_t NodeIdTable::IndexOf(NodeId id) const
{
    const Slot* slot = LiveSlot(id);
    return slot ? slot->index : NoIndex;
}

void NodeIdTable::RestoreFrom(NodeIdTable&& snapshot)
{
    // Slots the snapshot uses come back with their ids. Every generation either table gave out
    // stays given out, so ids handed out since the snapshot never resolve again.
    const size_t slotCount = (std::max)(m_slots.size(), snapshot.m_slots.size());
    snapshot.m_slots.resize(slotCount);
    for (size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
        Slot& restored = snapshot.m_slots[slotIndex];
        if (slotIndex < m_slots.size())
            restored.unusedGeneration = (std::max)(restored.unusedGeneration, m_slots[slotIndex].unusedGeneration);
        if (restored.index == NoIndex && restored.unusedGeneration < GenerationCount)
            restored.generation = static_cast<uint8_t>(restored.unusedGeneration);
    }
    m_slots = std::move(snapshot.m_slots);
    RebuildFreeList();
}

const NodeIdTable::Slot* NodeIdTable::LiveSlot(NodeId id) const
{
    // The invalid id wraps to the largest slot index
    const uint32_t slotIndex = id.Index() - 1;
    if (!id || slotIndex >= m_slots.size())
        return nullptr;
    const Slot& slot = m_slots[slotIndex];
    return slot.index != NoIndex && slot.generation == id.Generation() ? &slot : nullptr;
}

void NodeIdTable::RebuildFreeList()
{
    m_freeSlots.clear();
    for (size_t slotIndex = 0; slotIndex < m_slots.size(); ++slotIndex) {
        const Slot& slot = m_slots[slotIndex];
        if (slot.index == NoIndex && slot.unusedGeneration < GenerationCount)
            m_freeSlots.push_back(static_cast<uint32_t>(slotIndex));
    }
}

} // namespace elm
