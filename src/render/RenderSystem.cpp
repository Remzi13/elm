#include "render/RenderSystem.hpp"

#include "core/Debug.hpp"
#include "core/JobSystem.hpp"
#include "core/Log.hpp"
#include "core/Profiling.hpp"

#include "render/rhi/IRenderBackend.hpp"

#include <utility>

namespace elm::render {

RenderSystem::RenderSystem() = default;

RenderSystem::~RenderSystem()
{
    Shutdown();
}

auto RenderSystem::Init(const NativeWindow& window, Size surfaceSize) -> EngineResult<void>
{
    if (m_initialized) {
        return { };
    }

    m_surfaceSize = surfaceSize;
    m_backend = rhi::createRenderBackend();
    if (auto result = m_backend->Init(window, surfaceSize); !result) {
        m_backend.reset();
        return result;
    }

    if (!m_pipeline.Init(*m_backend))
        ERROR_MESSAGE(log::Category::Render, "RenderSystem", "Render pipeline initialized with errors, some passes are disabled");

    m_frames.Reopen();
    m_initialized = true;
    LOG_MESSAGE(log::Category::Render, "RenderSystem", "Render system initialized (%s backend).", String(m_backend->GetName()).c_str());
    return { };
}

FrameWriter RenderSystem::BeginFrame()
{
    ELM_ASSERT_THREAD(Update);
    if (!m_initialized)
        return {};
    return FrameWriter(m_frames.AcquireWrite());
}

void RenderSystem::SubmitFrame(FrameWriter& writer)
{
    ELM_ASSERT_THREAD(Update);
    auto* frame = std::exchange(writer.m_frame, nullptr);
    if (!frame)
        return;

    frame->frameIndex = m_submittedFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    // A minimized window reports an empty size: keep presenting at the last valid one
    if (frame->backbufferSize.IsEmpty())
        frame->backbufferSize = m_surfaceSize;
    m_surfaceSize = frame->backbufferSize;
    m_resources.TakeCommands(frame->resourceCommands);
    m_frames.Publish(frame);
}

void RenderSystem::StopRendering()
{
    m_frames.Close();
}

bool RenderSystem::RenderNextFrame(RenderedFrameInfo& info)
{
    ELM_ASSERT_THREAD(Render);
    info = {};
    auto* frame = m_frames.AcquireRead();
    if (!frame)
        return false;

    ExecuteFrame(*frame, info);
    m_frames.Release(frame);
    return true;
}

uint64_t RenderSystem::ApplySurfaceEvents(const OverlayFrame& overlay)
{
    uint64_t released = 0;
    for (const auto& event : overlay.surfaceEvents) {
        switch (event.type) {
        case OverlaySurfaceEvent::Type::Create:
            m_backend->CreateSurface(event.id, { event.nativeHandle, event.nativeDisplay }, event.width, event.height);
            break;
        case OverlaySurfaceEvent::Type::Destroy:
            m_backend->DestroySurface(event.id);
            ++released;
            break;
        case OverlaySurfaceEvent::Type::Resize:
            m_backend->ResizeSurface(event.id, event.width, event.height);
            break;
        }
    }
    return released;
}

void RenderSystem::ExecuteFrame(FrameSnapshot& frame, RenderedFrameInfo& info)
{
    ELM_PROFILE_SCOPE_N("Render Frame");
    info.frameIndex = frame.frameIndex;

    {
        ELM_PROFILE_SCOPE_N("Resource Commands");
        m_backend->ExecuteResourceCommands(frame.resourceCommands);
    }
    m_backend->ResizeSurface(MainSurface, frame.backbufferSize.width, frame.backbufferSize.height);
    info.releasedSurfaceCount = ApplySurfaceEvents(frame.overlay);

    {
        // Equal meshes become neighbours, so passes can draw each run with one instanced call
        ELM_PROFILE_SCOPE_N("Sort Draw Items");
        std::stable_sort(frame.drawItems.begin(), frame.drawItems.end(),
            [](const DrawItem& a, const DrawItem& b) { return a.mesh < b.mesh; });
    }

    m_pipeline.Render(*m_backend, frame);
    {
        const auto& graph = m_pipeline.GetGraphStats();
        std::lock_guard lock(m_statsMutex);
        m_stats.frameIndex = frame.frameIndex;
        m_stats.passCount = graph.passCount;
        m_stats.culledPassCount = graph.culledPassCount;
        m_stats.levelCount = graph.levelCount;
        m_stats.commandListCount = graph.commandListCount;
        m_stats.workerListCount = graph.workerListCount;
        m_stats.drawCalls = graph.drawCalls;
        m_stats.workerThreads = core::JobSystem::Get().GetWorkerCount();
        m_stats.queuedFrames = static_cast<uint32_t>(m_submittedFrames.load(std::memory_order_relaxed) - frame.frameIndex);
    }

    {
        ELM_PROFILE_SCOPE_N("Present");
        for (const auto& viewport : frame.overlay.viewports) {
            if (!viewport.isMain)
                m_backend->Present(viewport.id);
        }
        m_backend->Present(MainSurface);
    }
}

void RenderSystem::Shutdown()
{
    if (!m_initialized)
        return;

    // The render thread is stopped: frames it did not draw still carry resource commands, which
    // are dropped together with the resources below
    m_frames.Close();
    m_frames.DrainPublished([](FrameSnapshot& frame) { frame.Reset(); });
    ResourceCommandList pending;
    m_resources.TakeCommands(pending);

    m_pipeline.Release(*m_backend);

    m_backend->Shutdown();
    m_backend.reset();

    m_initialized = false;
    LOG_MESSAGE(log::Category::Render, "RenderSystem", "Shutdown completed.");
}

RenderStats RenderSystem::GetRenderStats() const
{
    std::lock_guard lock(m_statsMutex);
    return m_stats;
}

size_t RenderSystem::GetMemAllocated() const
{
    return m_backend ? m_backend->GetAllocatedMemory() : 0;
}

} // namespace elm::render
