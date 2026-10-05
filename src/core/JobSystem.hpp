#pragma once

#include "core/Std.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace elm::core {

    /// Process-wide worker pool. Without workers (not initialized) jobs run inline on the submitting thread.
    class JobSystem {
    public:
        using Job = std::function<void()>;

        static JobSystem& Get();

        JobSystem() = default;
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        /// workerCount == 0 picks hardware_concurrency - 2 (at least one worker).
        void Init(uint32_t workerCount = 0);
        void Shutdown();

        [[nodiscard]] uint32_t GetWorkerCount() const noexcept { return static_cast<uint32_t>(m_workers.size()); }

        void Submit(Job job);
        /// Runs one queued job on the calling thread. Returns false when the queue is empty.
        bool TryRunOne();

    private:
        void WorkerLoop(uint32_t index);

        Vector<std::thread> m_workers;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<Job, memory::Allocator<Job>> m_jobs;
        bool m_stop { false };
    };

    /// A set of jobs that can be waited on. Wait() executes queued jobs while it waits,
    /// so groups may be nested and waited on from worker threads.
    class TaskGroup {
    public:
        explicit TaskGroup(JobSystem& jobSystem = JobSystem::Get()) noexcept
            : m_jobSystem(jobSystem)
        {
        }
        ~TaskGroup() { Wait(); }

        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;

        void Run(std::function<void()> job);
        void Wait();

    private:
        void Finish();

        JobSystem& m_jobSystem;
        std::atomic<uint32_t> m_pending { 0 };
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };

    /// Calls fn(begin, end) for consecutive ranges of at most batchSize elements, in parallel.
    void parallelFor(size_t count, size_t batchSize, const std::function<void(size_t, size_t)>& fn);

} // namespace elm::core
