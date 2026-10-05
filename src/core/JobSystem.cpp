#include "core/JobSystem.hpp"

#include "core/Profiling.hpp"

#include <algorithm>
#include <chrono>
#include <string>

namespace elm::core {

    JobSystem& JobSystem::Get()
    {
        static JobSystem instance;
        return instance;
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    void JobSystem::Init(uint32_t workerCount)
    {
        if (!m_workers.empty())
            return;

        if (workerCount == 0) {
            const auto hardware = std::thread::hardware_concurrency();
            workerCount = hardware > 3 ? hardware - 2 : 1;
        }

        m_stop = false;
        m_workers.reserve(workerCount);
        for (uint32_t i = 0; i < workerCount; ++i)
            m_workers.emplace_back(&JobSystem::WorkerLoop, this, i);
    }

    void JobSystem::Shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable())
                worker.join();
        }
        m_workers.clear();

        // Jobs left in the queue still belong to someone waiting on a TaskGroup
        while (TryRunOne()) { }
    }

    void JobSystem::Submit(Job job)
    {
        if (m_workers.empty()) {
            job();
            return;
        }
        {
            std::lock_guard lock(m_mutex);
            m_jobs.push_back(std::move(job));
        }
        m_cv.notify_one();
    }

    bool JobSystem::TryRunOne()
    {
        Job job;
        {
            std::lock_guard lock(m_mutex);
            if (m_jobs.empty())
                return false;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        job();
        return true;
    }

    void JobSystem::WorkerLoop([[maybe_unused]] uint32_t index)
    {
#if defined(TRACY_ENABLE)
        const std::string name = "Worker " + std::to_string(index);
        ELM_PROFILE_THREAD(name.c_str());
#endif
        while (true) {
            Job job;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
                if (m_jobs.empty())
                    return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            job();
        }
    }

    void TaskGroup::Run(std::function<void()> job)
    {
        m_pending.fetch_add(1, std::memory_order_relaxed);
        m_jobSystem.Submit([this, job = std::move(job)] {
            job();
            Finish();
        });
    }

    void TaskGroup::Finish()
    {
        // The last access to the group happens under its lock: Wait() returns only after taking
        // the same lock, so the group (usually on the waiter's stack) outlives this call
        std::lock_guard lock(m_mutex);
        if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
            m_cv.notify_all();
    }

    void TaskGroup::Wait()
    {
        while (true) {
            if (m_pending.load(std::memory_order_acquire) != 0 && m_jobSystem.TryRunOne())
                continue;
            std::unique_lock lock(m_mutex);
            if (m_pending.load(std::memory_order_acquire) == 0)
                return;
            m_cv.wait_for(lock, std::chrono::microseconds(200));
        }
    }

    void parallelFor(size_t count, size_t batchSize, const std::function<void(size_t, size_t)>& fn)
    {
        if (count == 0)
            return;
        batchSize = (std::max)(batchSize, size_t { 1 });
        if (count <= batchSize) {
            fn(0, count);
            return;
        }

        TaskGroup group;
        for (size_t begin = batchSize; begin < count; begin += batchSize) {
            const size_t end = (std::min)(begin + batchSize, count);
            group.Run([&fn, begin, end] { fn(begin, end); });
        }
        fn(0, batchSize);
        group.Wait();
    }

} // namespace elm::core
