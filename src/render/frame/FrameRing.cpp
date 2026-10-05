#include "render/frame/FrameRing.hpp"

#include "core/Debug.hpp"
#include "core/Profiling.hpp"

namespace elm::render {

    size_t FrameRing::IndexOf(const FrameSnapshot* frame) const
    {
        const auto index = static_cast<size_t>(frame - m_slots.data());
        ELM_ASSERT(index < SlotCount);
        return index;
    }

    FrameSnapshot* FrameRing::AcquireWrite()
    {
        ELM_PROFILE_SCOPE_N("Wait For Free Frame Slot");
        std::unique_lock lock(m_mutex);
        size_t index = SlotCount;
        m_cv.wait(lock, [&] {
            if (m_closed)
                return true;
            for (size_t i = 0; i < SlotCount; ++i) {
                if (m_states[i] == State::Free) {
                    index = i;
                    return true;
                }
            }
            return false;
        });
        if (m_closed)
            return nullptr;

        m_states[index] = State::Writing;
        m_slots[index].Reset();
        return &m_slots[index];
    }

    void FrameRing::Publish(FrameSnapshot* frame)
    {
        {
            std::lock_guard lock(m_mutex);
            const size_t index = IndexOf(frame);
            ELM_ASSERT(m_states[index] == State::Writing);
            m_states[index] = State::Published;
            m_published.push_back(index);
        }
        m_cv.notify_all();
    }

    FrameSnapshot* FrameRing::AcquireRead()
    {
        ELM_PROFILE_SCOPE_N("Wait For Frame Snapshot");
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_closed || !m_published.empty(); });
        if (m_closed)
            return nullptr;

        const size_t index = m_published.front();
        m_published.pop_front();
        m_states[index] = State::Reading;
        return &m_slots[index];
    }

    void FrameRing::Release(FrameSnapshot* frame)
    {
        {
            std::lock_guard lock(m_mutex);
            const size_t index = IndexOf(frame);
            ELM_ASSERT(m_states[index] == State::Reading);
            m_states[index] = State::Free;
        }
        m_cv.notify_all();
    }

    void FrameRing::Close()
    {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    void FrameRing::Reopen()
    {
        std::lock_guard lock(m_mutex);
        m_closed = false;
    }

} // namespace elm::render
