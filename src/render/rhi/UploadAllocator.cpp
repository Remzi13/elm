#include "render/rhi/UploadAllocator.hpp"

#include <algorithm>

namespace elm::render::rhi {

    UploadAllocator::UploadAllocator()
    {
        for (size_t arena = 0; arena < static_cast<size_t>(UploadArena::Count); ++arena)
            m_arenas[arena].memory.resize(GetDefaultCapacity(static_cast<UploadArena>(arena)));
    }

    UploadAllocation UploadAllocator::Allocate(UploadArena arena, size_t size, size_t alignment)
    {
        if (size == 0)
            return {};
        auto& target = m_arenas[Index(arena)];
        const size_t capacity = target.memory.size();

        // Reserve size + alignment so the aligned block always fits in the reserved range
        const size_t reserved = size + alignment - 1;
        const size_t start = target.offset.fetch_add(reserved, std::memory_order_relaxed);
        const size_t aligned = (start + alignment - 1) & ~(alignment - 1);
        if (aligned + size > capacity)
            return {};

        UploadAllocation allocation;
        allocation.ref = { arena, static_cast<uint32_t>(aligned), static_cast<uint32_t>(size) };
        allocation.data = target.memory.data() + aligned;
        return allocation;
    }

    void UploadAllocator::Reset()
    {
        for (auto& arena : m_arenas)
            arena.offset.store(0, std::memory_order_relaxed);
    }

    size_t UploadAllocator::GetUsedSize(UploadArena arena) const noexcept
    {
        const auto& target = m_arenas[Index(arena)];
        return (std::min)(target.offset.load(std::memory_order_acquire), target.memory.size());
    }

} // namespace elm::render::rhi
