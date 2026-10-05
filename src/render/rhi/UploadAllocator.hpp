#pragma once

#include "core/Std.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace elm::render::rhi {

    enum class UploadArena : uint8_t {
        Vertex,
        Index,
        Constants,
        Count
    };

    /// Location of transient data written by a pass for the current frame.
    struct UploadRef {
        UploadArena arena { UploadArena::Vertex };
        uint32_t offset { 0 };
        uint32_t size { 0 };

        [[nodiscard]] bool IsValid() const noexcept { return size != 0; }
    };

    struct UploadAllocation {
        UploadRef ref;
        void* data { nullptr };

        [[nodiscard]] bool IsValid() const noexcept { return data != nullptr; }
    };

    /// CPU-side per-frame memory for vertices, indices and constants written while recording.
    /// Allocate() is lock-free and may be called from several recording jobs at once; the backend
    /// copies the used part of each arena to the GPU once per frame when the command lists are submitted.
    class UploadAllocator {
    public:
        [[nodiscard]] static constexpr size_t GetDefaultCapacity(UploadArena arena) noexcept
        {
            switch (arena) {
            case UploadArena::Vertex:
                return 32 * 1024 * 1024;
            case UploadArena::Index:
                return 8 * 1024 * 1024;
            default:
                return 1 * 1024 * 1024;
            }
        }

        UploadAllocator();

        UploadAllocator(const UploadAllocator&) = delete;
        UploadAllocator& operator=(const UploadAllocator&) = delete;

        /// Returns an invalid allocation when the arena is full for this frame.
        [[nodiscard]] UploadAllocation Allocate(UploadArena arena, size_t size, size_t alignment = 16);

        template <typename T>
        [[nodiscard]] UploadRef Upload(UploadArena arena, const T* data, size_t count)
        {
            auto allocation = Allocate(arena, sizeof(T) * count, alignof(T) < 16 ? 16 : alignof(T));
            if (allocation.IsValid())
                std::memcpy(allocation.data, data, sizeof(T) * count);
            return allocation.ref;
        }

        /// Render thread, between frames.
        void Reset();

        [[nodiscard]] const uint8_t* GetData(UploadArena arena) const noexcept { return m_arenas[Index(arena)].memory.data(); }
        [[nodiscard]] size_t GetUsedSize(UploadArena arena) const noexcept;
        [[nodiscard]] size_t GetCapacity(UploadArena arena) const noexcept { return m_arenas[Index(arena)].memory.size(); }

    private:
        static constexpr size_t Index(UploadArena arena) noexcept { return static_cast<size_t>(arena); }

        struct Arena {
            Vector<uint8_t> memory;
            std::atomic<size_t> offset { 0 };
        };
        Arena m_arenas[static_cast<size_t>(UploadArena::Count)];
    };

} // namespace elm::render::rhi
