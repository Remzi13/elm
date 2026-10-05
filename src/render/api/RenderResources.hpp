#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/api/ResourceCommands.hpp"

#include <mutex>

namespace elm::render {

    /// Update-side resource API. Every method may be called from any thread.
    ///
    /// Create* returns the handle immediately; the GPU object is created by the render thread
    /// before it draws the first frame submitted after the call. Commands keep their recording
    /// order (across threads: the order in which the calls acquired the internal lock) and are
    /// never dropped: RenderSystem moves them into the next submitted frame.
    class RenderResources {
    public:
        RenderResources() = default;
        RenderResources(const RenderResources&) = delete;
        RenderResources& operator=(const RenderResources&) = delete;

        [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& desc);
        /// Recreates the texture storage with a new size; contents are lost.
        void ResizeTexture(TextureHandle handle, uint32_t width, uint32_t height);
        void UpdateTexture(TextureHandle handle, TextureData data);
        void DestroyTexture(TextureHandle handle);

        [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& desc, Vector<uint8_t> initialData = {});
        void UpdateBuffer(BufferHandle handle, uint64_t offset, Vector<uint8_t> data);
        void DestroyBuffer(BufferHandle handle);

        [[nodiscard]] MeshHandle CreateMesh(MeshDataPtr data);
        [[nodiscard]] MeshHandle CreateMesh(const MeshData& data);
        void DestroyMesh(MeshHandle handle);

        [[nodiscard]] bool IsAlive(TextureHandle handle) const { return m_textures.IsAlive(handle); }
        [[nodiscard]] bool IsAlive(MeshHandle handle) const { return m_meshes.IsAlive(handle); }

        /// RenderSystem: moves the recorded commands to out (appends).
        void TakeCommands(ResourceCommandList& out);

    private:
        template <typename Command>
        void Record(Command&& command)
        {
            std::lock_guard lock(m_mutex);
            m_commands.Push(std::forward<Command>(command));
        }

        HandleAllocator<TextureHandle> m_textures;
        HandleAllocator<BufferHandle> m_buffers;
        HandleAllocator<MeshHandle> m_meshes;

        std::mutex m_mutex;
        ResourceCommandList m_commands;
    };

    /// RAII owner of a texture created through RenderResources (move-only).
    class Texture {
    public:
        Texture() = default;
        Texture(RenderResources& resources, const TextureDesc& desc);
        ~Texture() { Reset(); }

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept;
        Texture& operator=(Texture&& other) noexcept;

        [[nodiscard]] TextureHandle GetHandle() const noexcept { return m_handle; }
        [[nodiscard]] bool IsValid() const noexcept { return m_handle.IsValid(); }
        explicit operator bool() const noexcept { return IsValid(); }

        [[nodiscard]] const TextureDesc& GetDesc() const noexcept { return m_desc; }
        [[nodiscard]] uint32_t GetWidth() const noexcept { return m_desc.width; }
        [[nodiscard]] uint32_t GetHeight() const noexcept { return m_desc.height; }

        void Update(TextureData data);
        /// No-op when the size does not change.
        void Resize(uint32_t width, uint32_t height);
        void Reset();

    private:
        RenderResources* m_resources { nullptr };
        TextureHandle m_handle;
        TextureDesc m_desc;
    };

    /// RAII owner of a mesh created through RenderResources (move-only).
    class Mesh {
    public:
        Mesh() = default;
        Mesh(RenderResources& resources, MeshDataPtr data);
        ~Mesh() { Reset(); }

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&& other) noexcept;
        Mesh& operator=(Mesh&& other) noexcept;

        [[nodiscard]] MeshHandle GetHandle() const noexcept { return m_handle; }
        [[nodiscard]] const MeshDataPtr& GetData() const noexcept { return m_data; }
        [[nodiscard]] bool IsValid() const noexcept { return m_handle.IsValid(); }

        void Reset();

    private:
        RenderResources* m_resources { nullptr };
        MeshHandle m_handle;
        MeshDataPtr m_data;
    };

} // namespace elm::render
