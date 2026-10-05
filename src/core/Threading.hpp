#pragma once

#include <thread>
#include <functional>

namespace elm::core
{
    class Thread
    {
    public:
        Thread() = default;

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        void Start(std::function<void()> func);
        void Join();
    private:
        std::thread m_thread;
    };

    /// Engine threads with a fixed responsibility. API that may only be used from one of them
    /// checks it with ELM_ASSERT_THREAD.
    enum class ThreadRole
    {
        Update,
        Render,
        Count
    };

    /// Binds the calling thread to the role. Rebinding a role moves it to the calling thread.
    void registerThread(ThreadRole role);
    void unregisterThread(ThreadRole role);
    /// True when the calling thread holds the role, or when no thread has registered it yet.
    [[nodiscard]] bool isThread(ThreadRole role);
}
