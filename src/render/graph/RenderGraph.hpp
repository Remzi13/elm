#pragma once

#include "core/Std.hpp"

#include "render/api/Handles.hpp"
#include "render/api/ResourceDesc.hpp"
#include "render/rhi/CommandList.hpp"
#include "render/rhi/UploadAllocator.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace elm::render {

    struct FrameSnapshot;

    namespace rhi {
        class IRenderBackend;
    }

    class RenderGraph;
    class TransientTexturePool;

    /// Virtual texture of one frame's graph. Only meaningful for the graph that created it.
    struct RGTexture {
        static constexpr uint32_t InvalidIndex = (std::numeric_limits<uint32_t>::max)();
        uint32_t index { InvalidIndex };

        [[nodiscard]] bool IsValid() const noexcept { return index != InvalidIndex; }
        bool operator==(const RGTexture&) const noexcept = default;
    };

    /// Setup-time interface of a pass: declares what the pass reads and writes.
    class PassBuilder {
    public:
        /// Texture owned by the graph, allocated from a pool for the duration of the frame.
        [[nodiscard]] RGTexture CreateTexture(StringView name, const TextureDesc& desc);
        /// Sampled in a shader: the texture is transitioned to the shader resource state.
        RGTexture Read(RGTexture texture);
        /// Bound as the color target.
        RGTexture WriteColor(RGTexture texture);
        /// Bound as the depth target.
        RGTexture WriteDepth(RGTexture texture);
        /// Keeps the pass even when nothing reads its results.
        void SetSideEffect();

    private:
        friend class RenderGraph;
        PassBuilder(RenderGraph& graph, uint32_t pass) noexcept
            : m_graph(graph)
            , m_pass(pass)
        {
        }

        RenderGraph& m_graph;
        uint32_t m_pass;
    };

    /// Record-time interface of a pass. A pass may be recorded on a worker thread: it must only
    /// touch its own pass data, the frame snapshot (read-only) and this context.
    class PassContext {
    public:
        [[nodiscard]] rhi::CommandList& Commands() noexcept { return m_commands; }
        [[nodiscard]] rhi::UploadAllocator& Uploads() noexcept { return m_uploads; }
        [[nodiscard]] const FrameSnapshot& Frame() const noexcept { return m_frame; }

        [[nodiscard]] TextureHandle Resolve(RGTexture texture) const;
        [[nodiscard]] const TextureDesc& GetDesc(RGTexture texture) const;
        /// Color attachment for a texture or an imported surface.
        [[nodiscard]] rhi::ColorAttachment ColorTarget(RGTexture texture, bool clear, const float (&clearColor)[4]) const;
        [[nodiscard]] rhi::DepthAttachment DepthTarget(RGTexture texture, bool clear, float clearDepth = 1.0f) const;

        using ParallelRecordFn = std::function<void(rhi::CommandList& commands, size_t begin, size_t end)>;
        /// Splits [0, count) into batches and records each batch into its own command list, in
        /// parallel when the graph records in parallel. The lists are submitted right after this
        /// pass's own list, in batch order. Each list starts with a clean state: it must bind its
        /// targets, pipeline and resources itself.
        void RecordParallel(size_t count, size_t batchSize, const ParallelRecordFn& fn);

    private:
        friend class RenderGraph;
        PassContext(const RenderGraph& graph, rhi::CommandList& commands, Vector<rhi::CommandList>& subLists,
            rhi::UploadAllocator& uploads, const FrameSnapshot& frame, bool parallel) noexcept
            : m_graph(graph)
            , m_commands(commands)
            , m_subLists(subLists)
            , m_uploads(uploads)
            , m_frame(frame)
            , m_parallel(parallel)
        {
        }

        const RenderGraph& m_graph;
        rhi::CommandList& m_commands;
        Vector<rhi::CommandList>& m_subLists;
        rhi::UploadAllocator& m_uploads;
        const FrameSnapshot& m_frame;
        bool m_parallel { false };
    };

    /// Per-frame graph of render passes.
    ///
    /// Passes declare their resources in a setup callback and record commands in an execute
    /// callback. Compile() removes passes whose results are unused, orders the rest into dependency
    /// levels (passes of one level do not depend on each other) and computes the resource state
    /// transitions. Execute() records every level, optionally in parallel, and submits the command
    /// lists to the backend in dependency order.
    class RenderGraph {
    public:
        RenderGraph() = default;
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        /// Long-lived texture created elsewhere (e.g. by the update side).
        [[nodiscard]] RGTexture ImportTexture(StringView name, TextureHandle texture, const TextureDesc& desc);
        /// Backbuffer of a presentation surface. Writing it counts as a side effect.
        [[nodiscard]] RGTexture ImportSurface(StringView name, SurfaceId surface, Size size);

        template <typename Data, typename Setup, typename Execute>
        const Data& AddPass(StringView name, Setup&& setup, Execute&& execute)
        {
            static_assert(std::is_invocable_v<Setup, PassBuilder&, Data&>, "setup(PassBuilder&, Data&)");
            static_assert(std::is_invocable_v<Execute, const Data&, PassContext&>, "execute(const Data&, PassContext&)");

            auto pass = MakeUnique<TypedPass<Data, std::decay_t<Execute>>>(std::forward<Execute>(execute));
            auto& data = pass->data;
            const auto index = static_cast<uint32_t>(m_passes.size());
            auto& node = m_passes.emplace_back();
            node.name = name;
            node.callback = std::move(pass);

            PassBuilder builder(*this, index);
            setup(builder, data);
            return data;
        }

        void Compile();
        /// Records the compiled passes and submits them. parallel: record passes of a level on the job system.
        void Execute(rhi::IRenderBackend& backend, TransientTexturePool& pool, rhi::UploadAllocator& uploads,
            const FrameSnapshot& frame, bool parallel);
        void Reset();

        [[nodiscard]] const TextureDesc& GetDesc(RGTexture texture) const { return m_textures[texture.index].desc; }

        struct Stats {
            uint32_t passCount { 0 };
            uint32_t culledPassCount { 0 };
            uint32_t levelCount { 0 };
            uint32_t barrierCount { 0 };
            uint32_t commandListCount { 0 };
            /// Command lists recorded on job system workers rather than the render thread.
            uint32_t workerListCount { 0 };
            uint32_t drawCalls { 0 };
        };
        [[nodiscard]] const Stats& GetStats() const noexcept { return m_stats; }

    private:
        friend class PassBuilder;
        friend class PassContext;

        struct PassCallback {
            virtual ~PassCallback() = default;
            virtual void Execute(PassContext& context) = 0;
        };

        template <typename Data, typename Fn>
        struct TypedPass final : PassCallback {
            explicit TypedPass(Fn&& inFn)
                : fn(std::move(inFn))
            {
            }
            void Execute(PassContext& context) override { fn(static_cast<const Data&>(data), context); }
            Data data {};
            Fn fn;
        };

        enum class AccessType : uint8_t {
            Read,
            WriteColor,
            WriteDepth,
        };

        struct Access {
            uint32_t texture { 0 };
            AccessType type { AccessType::Read };
        };

        struct BarrierDesc {
            uint32_t texture { 0 };
            rhi::ResourceState state { rhi::ResourceState::ShaderResource };
        };

        struct PassNode {
            String name;
            UniquePtr<PassCallback> callback;
            Vector<Access> accesses;
            Vector<BarrierDesc> barriers;
            bool sideEffect { false };
            bool culled { false };
            uint32_t level { 0 };
        };

        enum class TextureKind : uint8_t {
            Transient,
            Imported,
            Surface,
        };

        struct TextureNode {
            String name;
            TextureDesc desc;
            TextureKind kind { TextureKind::Transient };
            TextureHandle handle;
            SurfaceId surface { MainSurface };
        };

        void AddAccess(uint32_t pass, RGTexture texture, AccessType type);
        void RecordPass(PassNode& pass, rhi::CommandList& commands, Vector<rhi::CommandList>& subLists,
            rhi::UploadAllocator& uploads, const FrameSnapshot& frame, bool parallel) const;
        void CountRecordedList() const;

        Vector<PassNode> m_passes;
        Vector<TextureNode> m_textures;
        Vector<Vector<uint32_t>> m_levels;
        bool m_compiled { false };
        Stats m_stats;
        mutable std::atomic<uint32_t> m_workerLists { 0 };
    };

    /// Keeps graph-owned textures alive across frames, so steady-state frames allocate nothing.
    /// Textures unused for a few frames are destroyed. Render thread only.
    class TransientTexturePool {
    public:
        [[nodiscard]] TextureHandle Acquire(rhi::IRenderBackend& backend, const TextureDesc& desc);
        /// End of frame: everything acquired this frame becomes available again.
        void EndFrame(rhi::IRenderBackend& backend);
        void Clear(rhi::IRenderBackend& backend);

    private:
        struct Entry {
            TextureHandle handle;
            TextureDesc desc;
            uint32_t unusedFrames { 0 };
            bool inUse { false };
        };

        static constexpr uint32_t MaxUnusedFrames = 8;

        Vector<Entry> m_entries;
    };

} // namespace elm::render
