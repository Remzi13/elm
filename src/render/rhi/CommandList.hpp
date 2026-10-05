#pragma once

#include "core/Std.hpp"

#include "render/api/Handles.hpp"
#include "render/rhi/UploadAllocator.hpp"

#include <variant>

namespace elm::render::rhi {

    enum class ResourceState : uint8_t {
        ShaderResource,
        RenderTarget,
        DepthWrite,
    };

    /// Where a vertex or index stream comes from.
    struct BufferSource {
        enum class Kind : uint8_t {
            None,
            MeshVertices,
            MeshIndices,
            Buffer,
            Upload,
        };

        Kind kind { Kind::None };
        MeshHandle mesh;
        BufferHandle buffer;
        uint64_t offset { 0 };
        UploadRef upload;

        [[nodiscard]] static BufferSource Vertices(MeshHandle mesh) noexcept { return { Kind::MeshVertices, mesh, {}, 0, {} }; }
        [[nodiscard]] static BufferSource Indices(MeshHandle mesh) noexcept { return { Kind::MeshIndices, mesh, {}, 0, {} }; }
        [[nodiscard]] static BufferSource FromBuffer(BufferHandle buffer, uint64_t offset = 0) noexcept { return { Kind::Buffer, {}, buffer, offset, {} }; }
        [[nodiscard]] static BufferSource FromUpload(UploadRef upload) noexcept { return { Kind::Upload, {}, {}, 0, upload }; }
    };

    struct ColorAttachment {
        /// Either a texture or a presentation surface backbuffer.
        TextureHandle texture;
        SurfaceId surface { MainSurface };
        bool useSurface { false };
        bool clear { false };
        float clearColor[4] { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct DepthAttachment {
        TextureHandle texture;
        bool clear { false };
        float clearDepth { 1.0f };
    };

    namespace cmd {
        struct Barrier {
            TextureHandle texture;
            ResourceState state { ResourceState::ShaderResource };
        };
        /// Binds the targets and sets the viewport to the full color target.
        struct BeginRenderPass {
            ColorAttachment color;
            DepthAttachment depth;
        };
        struct EndRenderPass { };
        struct SetPipeline {
            PipelineHandle pipeline;
        };
        struct SetViewport {
            float x { 0.0f };
            float y { 0.0f };
            float width { 0.0f };
            float height { 0.0f };
        };
        struct SetScissor {
            int32_t left { 0 };
            int32_t top { 0 };
            int32_t right { 0 };
            int32_t bottom { 0 };
        };
        struct BindVertexBuffer {
            uint32_t slot { 0 };
            BufferSource source;
        };
        struct BindIndexBuffer {
            BufferSource source;
        };
        /// slot: index of a BindingType::Texture entry in the pipeline bindings.
        struct BindTexture {
            uint32_t slot { 0 };
            TextureHandle texture;
            /// Used when texture does not exist (not created yet or already destroyed).
            TextureHandle fallback;
        };
        /// slot: index of a BindingType::Constants entry in the pipeline bindings.
        struct SetConstants {
            uint32_t slot { 0 };
            UploadRef data;
        };
        /// indexCount == 0 draws all indices of the bound mesh index stream. Indices are 32-bit.
        struct DrawIndexed {
            uint32_t indexCount { 0 };
            uint32_t instanceCount { 1 };
            uint32_t firstIndex { 0 };
            uint32_t baseVertex { 0 };
        };
    }

    using Command = std::variant<
        cmd::Barrier,
        cmd::BeginRenderPass,
        cmd::EndRenderPass,
        cmd::SetPipeline,
        cmd::SetViewport,
        cmd::SetScissor,
        cmd::BindVertexBuffer,
        cmd::BindIndexBuffer,
        cmd::BindTexture,
        cmd::SetConstants,
        cmd::DrawIndexed>;

    /// Backend-independent list of draw commands. A pass records into its own list (possibly on a
    /// worker thread); the backend translates the lists in submission order on the render thread.
    class CommandList {
    public:
        explicit CommandList(StringView name = {})
            : m_name(name)
        {
        }

        CommandList(CommandList&&) noexcept = default;
        CommandList& operator=(CommandList&&) noexcept = default;
        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;

        void Barrier(TextureHandle texture, ResourceState state) { m_commands.emplace_back(cmd::Barrier { texture, state }); }
        void BeginRenderPass(const ColorAttachment& color, const DepthAttachment& depth = {}) { m_commands.emplace_back(cmd::BeginRenderPass { color, depth }); }
        void EndRenderPass() { m_commands.emplace_back(cmd::EndRenderPass {}); }
        void SetPipeline(PipelineHandle pipeline) { m_commands.emplace_back(cmd::SetPipeline { pipeline }); }
        void SetViewport(float x, float y, float width, float height) { m_commands.emplace_back(cmd::SetViewport { x, y, width, height }); }
        void SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom) { m_commands.emplace_back(cmd::SetScissor { left, top, right, bottom }); }
        void BindVertexBuffer(uint32_t slot, const BufferSource& source) { m_commands.emplace_back(cmd::BindVertexBuffer { slot, source }); }
        void BindIndexBuffer(const BufferSource& source) { m_commands.emplace_back(cmd::BindIndexBuffer { source }); }
        void BindTexture(uint32_t slot, TextureHandle texture, TextureHandle fallback = {}) { m_commands.emplace_back(cmd::BindTexture { slot, texture, fallback }); }
        void SetConstants(uint32_t slot, UploadRef data) { m_commands.emplace_back(cmd::SetConstants { slot, data }); }
        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, uint32_t baseVertex = 0)
        {
            m_commands.emplace_back(cmd::DrawIndexed { indexCount, instanceCount, firstIndex, baseVertex });
        }

        [[nodiscard]] const Vector<Command>& GetCommands() const noexcept { return m_commands; }
        [[nodiscard]] StringView GetName() const noexcept { return m_name; }
        [[nodiscard]] bool Empty() const noexcept { return m_commands.empty(); }
        void Clear() { m_commands.clear(); }
        void Reserve(size_t count) { m_commands.reserve(count); }

    private:
        String m_name;
        Vector<Command> m_commands;
    };

} // namespace elm::render::rhi
