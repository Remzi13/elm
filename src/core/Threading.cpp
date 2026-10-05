#include "core/Threading.hpp"

#include <array>
#include <atomic>

namespace elm::core
{
    namespace
    {
        std::array<std::atomic<std::thread::id>, static_cast<size_t>(ThreadRole::Count)> g_threadRoles;
    }

    void Thread::Start(std::function<void()> func)
    {
        m_thread = std::thread(func);
    }

    void Thread::Join()
    {
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void registerThread(ThreadRole role)
    {
        g_threadRoles[static_cast<size_t>(role)].store(std::this_thread::get_id(), std::memory_order_release);
    }

    void unregisterThread(ThreadRole role)
    {
        g_threadRoles[static_cast<size_t>(role)].store(std::thread::id{}, std::memory_order_release);
    }

    bool isThread(ThreadRole role)
    {
        const auto owner = g_threadRoles[static_cast<size_t>(role)].load(std::memory_order_acquire);
        return owner == std::thread::id{} || owner == std::this_thread::get_id();
    }
}
