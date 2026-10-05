#include "render/graph/RenderGraph.hpp"

#include "core/Debug.hpp"
#include "core/JobSystem.hpp"
#include "core/Profiling.hpp"
#include "core/Threading.hpp"

#include "render/rhi/IRenderBackend.hpp"

#include <algorithm>

namespace elm::render {

// ─────────────────────────────────────────────────────────────────────────────
// PassBuilder / PassContext
// ─────────────────────────────────────────────────────────────────────────────

RGTexture PassBuilder::CreateTexture(StringView name, const TextureDesc& desc)
{
    RGTexture texture { static_cast<uint32_t>(m_graph.m_textures.size()) };
    auto& node = m_graph.m_textures.emplace_back();
    node.name = name;
    node.desc = desc;
    node.desc.name = name;
    node.kind = RenderGraph::TextureKind::Transient;
    return texture;
}

RGTexture PassBuilder::Read(RGTexture texture)
{
    m_graph.AddAccess(m_pass, texture, RenderGraph::AccessType::Read);
    return texture;
}

RGTexture PassBuilder::WriteColor(RGTexture texture)
{
    m_graph.AddAccess(m_pass, texture, RenderGraph::AccessType::WriteColor);
    return texture;
}

RGTexture PassBuilder::WriteDepth(RGTexture texture)
{
    m_graph.AddAccess(m_pass, texture, RenderGraph::AccessType::WriteDepth);
    return texture;
}

void PassBuilder::SetSideEffect()
{
    m_graph.m_passes[m_pass].sideEffect = true;
}

TextureHandle PassContext::Resolve(RGTexture texture) const
{
    return texture.IsValid() ? m_graph.m_textures[texture.index].handle : TextureHandle {};
}

const TextureDesc& PassContext::GetDesc(RGTexture texture) const
{
    return m_graph.m_textures[texture.index].desc;
}

rhi::ColorAttachment PassContext::ColorTarget(RGTexture texture, bool clear, const float (&clearColor)[4]) const
{
    rhi::ColorAttachment attachment;
    if (texture.IsValid()) {
        const auto& node = m_graph.m_textures[texture.index];
        attachment.useSurface = node.kind == RenderGraph::TextureKind::Surface;
        attachment.surface = node.surface;
        attachment.texture = node.handle;
    }
    attachment.clear = clear;
    std::copy(std::begin(clearColor), std::end(clearColor), attachment.clearColor);
    return attachment;
}

rhi::DepthAttachment PassContext::DepthTarget(RGTexture texture, bool clear, float clearDepth) const
{
    return { Resolve(texture), clear, clearDepth };
}

void PassContext::RecordParallel(size_t count, size_t batchSize, const ParallelRecordFn& fn)
{
    if (count == 0)
        return;
    batchSize = (std::max)(batchSize, size_t { 1 });
    const size_t batchCount = (count + batchSize - 1) / batchSize;
    const size_t first = m_subLists.size();
    for (size_t i = 0; i < batchCount; ++i)
        m_subLists.emplace_back(m_commands.GetName());

    const auto recordBatch = [&](size_t batch) {
        ELM_PROFILE_SCOPE_N("Record Pass Batch");
        const size_t begin = batch * batchSize;
        fn(m_subLists[first + batch], begin, (std::min)(begin + batchSize, count));
        m_graph.CountRecordedList();
    };

    if (m_parallel && batchCount > 1) {
        core::parallelFor(batchCount, 1, [&](size_t begin, size_t end) {
            for (size_t batch = begin; batch < end; ++batch)
                recordBatch(batch);
        });
    } else {
        for (size_t batch = 0; batch < batchCount; ++batch)
            recordBatch(batch);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RenderGraph
// ─────────────────────────────────────────────────────────────────────────────

RGTexture RenderGraph::ImportTexture(StringView name, TextureHandle texture, const TextureDesc& desc)
{
    RGTexture result { static_cast<uint32_t>(m_textures.size()) };
    auto& node = m_textures.emplace_back();
    node.name = name;
    node.desc = desc;
    node.kind = TextureKind::Imported;
    node.handle = texture;
    return result;
}

RGTexture RenderGraph::ImportSurface(StringView name, SurfaceId surface, Size size)
{
    RGTexture result { static_cast<uint32_t>(m_textures.size()) };
    auto& node = m_textures.emplace_back();
    node.name = name;
    node.desc.width = size.width;
    node.desc.height = size.height;
    node.desc.bindFlags = TextureBind::RenderTarget;
    node.kind = TextureKind::Surface;
    node.surface = surface;
    return result;
}

void RenderGraph::AddAccess(uint32_t pass, RGTexture texture, AccessType type)
{
    ELM_ASSERT(texture.IsValid() && texture.index < m_textures.size());
    if (texture.IsValid())
        m_passes[pass].accesses.push_back({ texture.index, type });
}

void RenderGraph::Compile()
{
    ELM_PROFILE_SCOPE_N("Render Graph Compile");
    m_stats = {};
    m_stats.passCount = static_cast<uint32_t>(m_passes.size());

    // 1. Culling, back to front: a pass is needed when it has side effects, writes something that
    //    outlives the graph, or writes a texture a needed pass reads
    Vector<bool> needed(m_textures.size(), false);
    for (size_t i = m_passes.size(); i-- > 0;) {
        auto& pass = m_passes[i];
        bool alive = pass.sideEffect;
        for (const auto& access : pass.accesses) {
            if (access.type == AccessType::Read)
                continue;
            const auto kind = m_textures[access.texture].kind;
            alive = alive || kind != TextureKind::Transient || needed[access.texture];
        }
        pass.culled = !alive;
        if (!alive) {
            ++m_stats.culledPassCount;
            continue;
        }
        for (const auto& access : pass.accesses) {
            if (access.type == AccessType::Read)
                needed[access.texture] = true;
        }
    }

    // 2. Dependency levels: after the last writer of everything a pass touches, and after the
    //    readers of everything it writes (write-after-read)
    struct TextureTracking {
        uint32_t writerLevel { 0 };
        bool written { false };
        uint32_t readersLevel { 0 };
        bool read { false };
        rhi::ResourceState state { rhi::ResourceState::ShaderResource };
        bool stateKnown { false };
    };
    Vector<TextureTracking> tracking(m_textures.size());

    m_levels.clear();
    for (uint32_t index = 0; index < m_passes.size(); ++index) {
        auto& pass = m_passes[index];
        if (pass.culled)
            continue;

        uint32_t level = 0;
        for (const auto& access : pass.accesses) {
            const auto& track = tracking[access.texture];
            if (track.written)
                level = (std::max)(level, track.writerLevel + 1);
            if (access.type != AccessType::Read && track.read)
                level = (std::max)(level, track.readersLevel + 1);
        }
        pass.level = level;
        if (m_levels.size() <= level)
            m_levels.resize(static_cast<size_t>(level) + 1);
        m_levels[level].push_back(index);

        // 3. State transitions, in the order passes are submitted
        pass.barriers.clear();
        for (const auto& access : pass.accesses) {
            auto& track = tracking[access.texture];
            if (access.type == AccessType::Read) {
                track.read = true;
                track.readersLevel = (std::max)(track.readersLevel, level);
            } else {
                track.written = true;
                track.writerLevel = level;
                track.read = false;
                track.readersLevel = 0;
            }

            // Surfaces are transitioned by the backend when they are bound for presentation
            if (m_textures[access.texture].kind == TextureKind::Surface)
                continue;
            const auto state = access.type == AccessType::Read ? rhi::ResourceState::ShaderResource
                : access.type == AccessType::WriteColor        ? rhi::ResourceState::RenderTarget
                                                               : rhi::ResourceState::DepthWrite;
            if (!track.stateKnown || track.state != state) {
                pass.barriers.push_back({ access.texture, state });
                track.state = state;
                track.stateKnown = true;
            }
        }
        m_stats.barrierCount += static_cast<uint32_t>(pass.barriers.size());
    }

    // Passes inside a level keep declaration order, which keeps submission deterministic
    m_stats.levelCount = static_cast<uint32_t>(m_levels.size());
    m_compiled = true;
}

void RenderGraph::CountRecordedList() const
{
    if (!core::isThread(core::ThreadRole::Render))
        m_workerLists.fetch_add(1, std::memory_order_relaxed);
}

void RenderGraph::RecordPass(PassNode& pass, rhi::CommandList& commands, Vector<rhi::CommandList>& subLists,
    rhi::UploadAllocator& uploads, const FrameSnapshot& frame, bool parallel) const
{
    ELM_PROFILE_SCOPE_N("Record Pass");
    ELM_PROFILE_TEXT(pass.name.c_str(), pass.name.size());
    for (const auto& barrier : pass.barriers)
        commands.Barrier(m_textures[barrier.texture].handle, barrier.state);

    PassContext context(*this, commands, subLists, uploads, frame, parallel);
    pass.callback->Execute(context);
    CountRecordedList();
}

void RenderGraph::Execute(rhi::IRenderBackend& backend, TransientTexturePool& pool, rhi::UploadAllocator& uploads,
    const FrameSnapshot& frame, bool parallel)
{
    ELM_PROFILE_SCOPE_N("Render Graph Execute");
    if (!m_compiled)
        Compile();

    // Transient textures live for the whole frame; the pool recycles them between frames
    for (auto& texture : m_textures) {
        if (texture.kind == TextureKind::Transient)
            texture.handle = pool.Acquire(backend, texture.desc);
    }

    // One main list per pass plus the batches the pass records through RecordParallel
    struct PassLists {
        rhi::CommandList main;
        Vector<rhi::CommandList> batches;
    };
    Vector<PassLists> lists;
    lists.reserve(m_passes.size());
    m_workerLists.store(0, std::memory_order_relaxed);

    for (const auto& level : m_levels) {
        const size_t first = lists.size();
        for (const uint32_t passIndex : level)
            lists.push_back({ rhi::CommandList(m_passes[passIndex].name), {} });

        // Passes of one level do not depend on each other
        if (parallel && level.size() > 1) {
            core::TaskGroup group;
            for (size_t i = 0; i < level.size(); ++i) {
                auto* pass = &m_passes[level[i]];
                auto* passLists = &lists[first + i];
                group.Run([this, pass, passLists, &uploads, &frame] {
                    RecordPass(*pass, passLists->main, passLists->batches, uploads, frame, true);
                });
            }
            group.Wait();
        } else {
            for (size_t i = 0; i < level.size(); ++i)
                RecordPass(m_passes[level[i]], lists[first + i].main, lists[first + i].batches, uploads, frame, parallel);
        }
    }

    Vector<const rhi::CommandList*> submission;
    for (const auto& passLists : lists) {
        submission.push_back(&passLists.main);
        for (const auto& batch : passLists.batches)
            submission.push_back(&batch);
    }
    m_stats.commandListCount = static_cast<uint32_t>(submission.size());
    m_stats.workerListCount = m_workerLists.load(std::memory_order_relaxed);

    {
        ELM_PROFILE_SCOPE_N("Backend Submit");
        m_stats.drawCalls = backend.Submit(submission, uploads).drawCalls;
    }
}

void RenderGraph::Reset()
{
    m_passes.clear();
    m_textures.clear();
    m_levels.clear();
    m_compiled = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// TransientTexturePool
// ─────────────────────────────────────────────────────────────────────────────

namespace {

    bool isCompatible(const TextureDesc& a, const TextureDesc& b)
    {
        return a.width == b.width && a.height == b.height && a.format == b.format &&
            a.bindFlags == b.bindFlags && a.usage == b.usage;
    }

} // namespace

TextureHandle TransientTexturePool::Acquire(rhi::IRenderBackend& backend, const TextureDesc& desc)
{
    for (auto& entry : m_entries) {
        if (!entry.inUse && isCompatible(entry.desc, desc)) {
            entry.inUse = true;
            entry.unusedFrames = 0;
            return entry.handle;
        }
    }

    const auto handle = backend.CreateTransientTexture(desc);
    if (handle.IsValid())
        m_entries.push_back({ handle, desc, 0, true });
    return handle;
}

void TransientTexturePool::EndFrame(rhi::IRenderBackend& backend)
{
    std::erase_if(m_entries, [&backend](Entry& entry) {
        if (entry.inUse) {
            entry.inUse = false;
            return false;
        }
        if (++entry.unusedFrames < MaxUnusedFrames)
            return false;
        backend.DestroyTransientTexture(entry.handle);
        return true;
    });
}

void TransientTexturePool::Clear(rhi::IRenderBackend& backend)
{
    for (const auto& entry : m_entries)
        backend.DestroyTransientTexture(entry.handle);
    m_entries.clear();
}

} // namespace elm::render
