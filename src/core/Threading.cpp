#include "core/Threading.hpp"

namespace elm::core
{
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
}