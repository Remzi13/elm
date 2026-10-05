#pragma once

#include "render/frame/FrameSnapshot.hpp"

#include <array>
#include <condition_variable>
#include <mutex>

namespace elm::render {

    /// Hands frame snapshots from the update thread to the render thread.
    ///
    /// With 3 slots the render thread draws frame N while the update thread builds N+1 and N+2 may
    /// wait in the queue; the update thread blocks only when it is two frames ahead. Frames are
    /// consumed strictly in order, none is skipped.
    class FrameRing {
    public:
        static constexpr size_t SlotCount = 3;

        /// Update thread. Blocks until a slot is free; nullptr once the ring is closed.
        [[nodiscard]] FrameSnapshot* AcquireWrite();
        void Publish(FrameSnapshot* frame);

        /// Render thread. Blocks until a frame is published; nullptr once the ring is closed.
        [[nodiscard]] FrameSnapshot* AcquireRead();
        void Release(FrameSnapshot* frame);

        /// Wakes both sides; later acquires return nullptr until Reopen().
        void Close();
        void Reopen();

        /// Any thread, when neither side is active: discards published frames and returns them.
        template <typename Fn>
        void DrainPublished(Fn&& fn)
        {
            std::lock_guard lock(m_mutex);
            for (const size_t index : m_published) {
                fn(m_slots[index]);
                m_states[index] = State::Free;
            }
            m_published.clear();
        }

    private:
        enum class State {
            Free,
            Writing,
            Published,
            Reading
        };

        [[nodiscard]] size_t IndexOf(const FrameSnapshot* frame) const;

        std::array<FrameSnapshot, SlotCount> m_slots;
        std::array<State, SlotCount> m_states { State::Free, State::Free, State::Free };
        Deque<size_t> m_published;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_closed { false };
    };

} // namespace elm::render
