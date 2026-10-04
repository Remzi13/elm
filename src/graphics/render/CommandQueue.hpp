#pragma once

#include "graphics/render/Command.hpp"
#include "graphics/render/CommandList.hpp"

#include <array>
#include <atomic>
#include <variant>
#include <vector>

namespace elm {
namespace render {

    using DeferredCommandList = BasicCommandList<std::variant<
        command::resource::UploadTexture,
        command::resource::CreateTexture,
        command::resource::DestroyTexture,
        command::resource::CreateMesh,
        command::resource::DestroyMesh,
        command::resource::ResizeMainSwapChain>>;

    /// Triple-buffered handoff for commands recorded on the producer thread and consumed
    /// by the render thread. This transfers commands; it does not schedule render passes.
    ///
    /// Thread safety contract:
    ///   - Push()       is called ONLY by the producer (Update thread).
    ///   - CommitFrame() is called by the producer after building the command list.
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

        /// Called by Producer (Update thread) to append a command to its private buffer.
        template <typename Command>
        void Push(Command&& command)
        {
            m_producerBuffer->Push(std::forward<Command>(command));
        }

        /// Called by Producer (Update thread) after building the command list.
        /// Atomically publishes the producer buffer for consumption.
        void CommitFrame()
        {
            if (!m_producerBuffer->Empty()) {
                DeferredCommandList* oldCommitted = m_committedBuffer.exchange(m_producerBuffer, std::memory_order_release);
                m_producerBuffer = (oldCommitted && oldCommitted != m_consumerBuffer) ? oldCommitted : GetFreeBuffer();
                m_producerBuffer->Clear();
            }
        }

        /// Called by Consumer (Render thread) at the start of each frame.
        /// Atomically claims the latest committed command buffer.
        void BeginFrame()
        {
            DeferredCommandList* newCommitted = m_committedBuffer.exchange(nullptr, std::memory_order_acquire);
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
        DeferredCommandList* GetFreeBuffer()
        {
            for (auto& buf : m_buffers) {
                if (&buf != m_consumerBuffer && &buf != m_committedBuffer.load(std::memory_order_relaxed)) {
                    return &buf;
                }
            }
            return &m_buffers[0];
        }

    private:
        std::array<DeferredCommandList, 3> m_buffers;
        DeferredCommandList* m_producerBuffer { nullptr };
        DeferredCommandList* m_consumerBuffer { nullptr };
        std::atomic<DeferredCommandList*> m_committedBuffer { nullptr };
    };



} // namespace render
} // namespace elm
