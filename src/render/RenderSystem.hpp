#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "math/Primitivs.hpp"

#include "render/RenderPipeline.hpp"
#include "render/api/FrameWriter.hpp"
#include "render/api/NativeWindow.hpp"
#include "render/api/RenderResources.hpp"
#include "render/frame/FrameRing.hpp"

#include <atomic>
#include <mutex>

namespace elm::render {

    namespace rhi {
        class IRenderBackend;
    }

    /// Result of one rendered frame, reported back to the update side.
    struct RenderedFrameInfo {
        uint64_t frameIndex { 0 };
        /// Secondary UI surfaces destroyed while executing this frame.
        uint64_t releasedSurfaceCount { 0 };
    };

    /// Render thread statistics of the last drawn frame, readable from any thread.
    struct RenderStats {
        uint64_t frameIndex { 0 };
        uint32_t passCount { 0 };
        uint32_t culledPassCount { 0 };
        uint32_t levelCount { 0 };
        uint32_t commandListCount { 0 };
        uint32_t workerListCount { 0 };
        uint32_t drawCalls { 0 };
        uint32_t workerThreads { 0 };
        uint32_t queuedFrames { 0 };
    };

    /// Facade of the render subsystem.
    ///
    /// Threading contract:
    ///  - Update thread: Init, BeginFrame/SubmitFrame, StopRendering, Shutdown, GetSize.
    ///  - Render thread: RenderNextFrame only.
    ///  - Any thread: Resources() and everything on RenderResources.
    class RenderSystem {
    public:
        RenderSystem();
        ~RenderSystem();

        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;
        RenderSystem(RenderSystem&&) noexcept = delete;
        RenderSystem& operator=(RenderSystem&&) noexcept = delete;

        /// The window stays owned by the caller and must outlive the render system.
        [[nodiscard]] auto Init(const NativeWindow& window, Size surfaceSize) -> EngineResult<void>;
        void Shutdown();

        // --- Any thread ---
        [[nodiscard]] RenderResources& Resources() noexcept { return m_resources; }

        // --- Update thread ---
        /// Blocks while the render thread is two frames behind. Returns an invalid writer after StopRendering().
        [[nodiscard]] FrameWriter BeginFrame();
        /// Publishes the frame together with every resource command recorded so far.
        void SubmitFrame(FrameWriter& frame);
        /// Wakes the render thread so RenderNextFrame returns false; the caller then joins it.
        void StopRendering();

        /// Size of the main surface (backbuffer pixels) of the last submitted frame.
        [[nodiscard]] Size GetSize() const { return m_surfaceSize; }
        [[nodiscard]] size_t GetMemAllocated() const;
        [[nodiscard]] RenderStats GetRenderStats() const;

        // --- Render thread ---
        /// Waits for the next submitted frame and draws it. Returns false once rendering is stopped.
        [[nodiscard]] bool RenderNextFrame(RenderedFrameInfo& info);

    private:
        void ExecuteFrame(FrameSnapshot& frame, RenderedFrameInfo& info);
        [[nodiscard]] uint64_t ApplySurfaceEvents(const OverlayFrame& overlay);

        Size m_surfaceSize { 1280, 720 };
        bool m_initialized { false };

        RenderResources m_resources;
        FrameRing m_frames;
        std::atomic<uint64_t> m_submittedFrames { 0 };

        UniquePtr<rhi::IRenderBackend> m_backend;
        RenderPipeline m_pipeline;

        mutable std::mutex m_statsMutex;
        RenderStats m_stats;
    };

} // namespace elm::render
