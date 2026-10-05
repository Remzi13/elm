#pragma once

#include "core/Debug.hpp"
#include "core/Std.hpp"

#include "render/api/Handles.hpp"

#include <mutex>

namespace elm::render {

    /// Thread-safe handle allocator: free slots are reused with a bumped generation.
    template <typename HandleT>
    class HandleAllocator {
    public:
        [[nodiscard]] HandleT Allocate()
        {
            std::lock_guard lock(m_mutex);
            uint32_t index = 0;
            if (!m_freeList.empty()) {
                index = m_freeList.back();
                m_freeList.pop_back();
            } else {
                // Slot 0 with generation 0 would be the invalid handle, so slots start at 1
                index = static_cast<uint32_t>(m_generations.size()) + 1;
                ELM_ASSERT(index <= HandleT::MaxIndex);
                m_generations.push_back(0);
            }
            return HandleT(index, m_generations[index - 1]);
        }

        void Free(HandleT handle)
        {
            if (!handle.IsValid())
                return;
            std::lock_guard lock(m_mutex);
            const uint32_t index = handle.Index();
            if (index == 0 || index > m_generations.size() || m_generations[index - 1] != handle.Generation())
                return;
            ++m_generations[index - 1];
            m_freeList.push_back(index);
        }

        [[nodiscard]] bool IsAlive(HandleT handle) const
        {
            std::lock_guard lock(m_mutex);
            const uint32_t index = handle.Index();
            return handle.IsValid() && index > 0 && index <= m_generations.size() &&
                m_generations[index - 1] == handle.Generation();
        }

    private:
        mutable std::mutex m_mutex;
        Vector<uint8_t> m_generations;
        Vector<uint32_t> m_freeList;
    };

    /// Dense handle -> value table used by backends on the render thread. Not thread-safe.
    template <typename HandleT, typename T>
    class ResourceRegistry {
    public:
        T& Insert(HandleT handle, T value)
        {
            const uint32_t index = handle.Index();
            if (index >= m_slots.size())
                m_slots.resize(static_cast<size_t>(index) + 1);
            auto& slot = m_slots[index];
            slot.generation = handle.Generation();
            slot.alive = true;
            slot.value = std::move(value);
            return slot.value;
        }

        [[nodiscard]] T* Find(HandleT handle)
        {
            const uint32_t index = handle.Index();
            if (!handle.IsValid() || index >= m_slots.size())
                return nullptr;
            auto& slot = m_slots[index];
            return slot.alive && slot.generation == handle.Generation() ? &slot.value : nullptr;
        }

        [[nodiscard]] const T* Find(HandleT handle) const
        {
            return const_cast<ResourceRegistry*>(this)->Find(handle);
        }

        /// Moves the value out and frees the slot. Returns false when the handle is stale.
        bool Remove(HandleT handle, T& out)
        {
            T* value = Find(handle);
            if (!value)
                return false;
            out = std::move(*value);
            *value = T {};
            m_slots[handle.Index()].alive = false;
            return true;
        }

        template <typename Fn>
        void ForEach(Fn&& fn)
        {
            for (auto& slot : m_slots) {
                if (slot.alive)
                    fn(slot.value);
            }
        }

        void Clear() { m_slots.clear(); }

    private:
        struct Slot {
            T value {};
            uint8_t generation { 0 };
            bool alive { false };
        };
        Vector<Slot> m_slots;
    };

} // namespace elm::render
