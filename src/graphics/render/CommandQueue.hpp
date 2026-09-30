#pragma once

#include "graphics/render/Command.hpp"

#include <array>
#include <atomic>
#include <variant>

namespace elm {
namespace render {

    /// Triple-buffered command queue for pipelined Update/Render.
    ///
    /// Thread safety contract:
    ///   - Push()       is called ONLY from the update (producer) thread
    ///   - Execute()    is called ONLY from the render (consumer) thread
    ///   - BeginFrame() is called from the render thread to swap buffers
    ///
    /// The triple buffer ensures that the producer can always write to a
    /// buffer that is neither being read nor about to be read.    
    class CommandQueue {
    public:
        template <typename Command>
        void Push(Command&& command)
        {
            m_commandLists[m_writeIndex.load(std::memory_order_acquire)].Push(std::forward<Command>(command));
        }

        /// Called from the render thread at the start of each frame.
        /// Swaps the write and execute indices so that:
        ///   - the render thread executes commands accumulated so far
        ///   - the update thread starts writing into a fresh buffer
        void BeginFrame()
        {
            // The buffer the producer was writing to becomes the one to execute.
            size_t prevWrite = m_writeIndex.load(std::memory_order_acquire);
            m_executeIndex = prevWrite;

            // Advance the write index to a free buffer (not the one being executed,
            // and not the one that was previously executed — which may still be in
            // flight on the GPU for the previous frame).
            size_t nextWrite = (prevWrite + 1) % m_commandLists.size();
            m_writeIndex.store(nextWrite, std::memory_order_release);

            // Clear the new write buffer so the producer starts fresh.
            m_commandLists[nextWrite].Clear();
        }

        template <class Executor>
        void Execute(Executor& executor)
        {
            m_commandLists[m_executeIndex].Execute(executor);            
        }

    private:
        std::array<CommandList, 3> m_commandLists;
        std::atomic<size_t> m_writeIndex { 0 };
        size_t m_executeIndex { 0 };
    };

} // namespace render
} // namespace elm
