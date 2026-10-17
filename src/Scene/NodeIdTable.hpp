#pragma once

#include "core/Std.hpp"

#include "Scene/NodeId.hpp"

namespace elm {

/// Gives out the NodeIds of a scene and knows the node index of every live one. Owner: Scene, which
/// reports every node that moves. A freed id stops resolving: its slot comes back under a generation
/// no id of the slot ever had, and a slot that ran out of generations is retired for good.
class NodeIdTable {
public:
    static constexpr size_t NoIndex = ~size_t { 0 };

    [[nodiscard]] NodeId Allocate(size_t nodeIndex);
    /// False for an id that is not live.
    bool Free(NodeId id);
    /// The node of a live id is now at nodeIndex.
    void Move(NodeId id, size_t nodeIndex);
    /// Node index of a live id, NoIndex for a freed, stale or foreign one.
    [[nodiscard]] size_t IndexOf(NodeId id) const;

    /// Becomes the snapshot again: its ids resolve as they did. Ids given out since the snapshot was
    /// taken stay stale, also after a node of the snapshot gives its slot back later.
    void RestoreFrom(NodeIdTable&& snapshot);

private:
    struct Slot {
        size_t index { NoIndex };
        /// Generation of the live id, or the one the next id of a free slot gets.
        uint8_t generation { 0 };
        /// Lowest generation the slot never gave out; past the last one the slot is retired.
        uint32_t unusedGeneration { 0 };
    };

    [[nodiscard]] const Slot* LiveSlot(NodeId id) const;
    void RebuildFreeList();

    Vector<Slot> m_slots;
    Vector<uint32_t> m_freeSlots;
};

} // namespace elm
