#pragma once

#include "graphics/render/Command.hpp"

#include <array>
#include <atomic>
#include <variant>

namespace elm {
namespace render {

    /// 100% Lock-Free double/triple-buffered command queue for pipelined Update/Render.
    ///
    /// Thread safety contract:
    ///   - Push()       is called ONLY by the producer (Update thread). No locks, no atomic ops.
    ///   - CommitFrame() is called by the producer at the end of Update to publish the command list.
    ///   - BeginFrame() is called by the consumer (Render thread) to claim the published commands.
    ///   - Execute()    is called by the consumer (Render thread) to execute commands.
    class CommandQueue {
    public:
        CommandQueue()
            : m_producerBuffer(&m_buffers[0])
            , m_consumerBuffer(&m_buffers[1])
            , m_committedBuffer(nullptr)
        {
        }

        ~CommandQueue() = default;

        CommandQueue(const CommandQueue&) = delete;
        CommandQueue& operator=(const CommandQueue&) = delete;

        /// Called by Producer (Update thread) to push a command. Zero locks, zero atomic ops.
        template <typename Command>
        void Push(Command&& command)
        {
            m_producerBuffer->Push(std::forward<Command>(command));
        }

        /// Called by Producer (Update thread) when a frame's command building is complete.
        /// Atomically publishes the producer buffer for consumption.
        void CommitFrame()
        {
            if (!m_producerBuffer->Empty()) {
                CommandList* oldCommitted = m_committedBuffer.exchange(m_producerBuffer, std::memory_order_release);
                m_producerBuffer = (oldCommitted && oldCommitted != m_consumerBuffer) ? oldCommitted : GetFreeBuffer();
                m_producerBuffer->Clear();
            }
        }

        /// Called by Consumer (Render thread) at the start of each frame.
        /// Atomically claims the latest committed command buffer.
        void BeginFrame()
        {
            CommandList* newCommitted = m_committedBuffer.exchange(nullptr, std::memory_order_acquire);
            if (newCommitted) {
                m_consumerBuffer = newCommitted;
            }
        }

        /// Called by Consumer (Render thread) to execute commands.
        template <class Executor>
        void Execute(Executor& executor)
        {
            if (m_consumerBuffer) {
                m_consumerBuffer->Execute(executor);
                // BeginFrame() keeps this buffer when nothing new was committed: never execute commands twice
                m_consumerBuffer->Clear();
            }
        }

    private:
        CommandList* GetFreeBuffer()
        {
            for (auto& buf : m_buffers) {
                if (&buf != m_consumerBuffer && &buf != m_committedBuffer.load(std::memory_order_relaxed)) {
                    return &buf;
                }
            }
            return &m_buffers[0];
        }

    private:
        std::array<CommandList, 3> m_buffers;
        CommandList* m_producerBuffer { nullptr };
        CommandList* m_consumerBuffer { nullptr };
        std::atomic<CommandList*> m_committedBuffer { nullptr };
    };



} // namespace render
} // namespace elm
