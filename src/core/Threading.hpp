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
}