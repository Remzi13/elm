#include "render/api/RenderResources.hpp"

#include <utility>

namespace elm::render {

    namespace cmd = command::resource;

    TextureHandle RenderResources::CreateTexture(const TextureDesc& desc)
    {
        const auto handle = m_textures.Allocate();
        Record(cmd::CreateTexture { handle, desc });
        return handle;
    }

    void RenderResources::ResizeTexture(TextureHandle handle, uint32_t width, uint32_t height)
    {
        if (handle.IsValid() && width > 0 && height > 0)
            Record(cmd::ResizeTexture { handle, width, height });
    }

    void RenderResources::UpdateTexture(TextureHandle handle, TextureData data)
    {
        if (handle.IsValid() && !data.data.empty())
            Record(cmd::UploadTexture { handle, std::move(data) });
    }

    void RenderResources::DestroyTexture(TextureHandle handle)
    {
        if (!handle.IsValid())
            return;
        // The command is recorded before the slot can be reused, so the render thread
        // always sees Destroy(old) before Create(new) for the same slot
        Record(cmd::DestroyTexture { handle });
        m_textures.Free(handle);
    }

    BufferHandle RenderResources::CreateBuffer(const BufferDesc& desc, Vector<uint8_t> initialData)
    {
        const auto handle = m_buffers.Allocate();
        Record(cmd::CreateBuffer { handle, desc, std::move(initialData) });
        return handle;
    }

    void RenderResources::UpdateBuffer(BufferHandle handle, uint64_t offset, Vector<uint8_t> data)
    {
        if (handle.IsValid() && !data.empty())
            Record(cmd::UploadBuffer { handle, offset, std::move(data) });
    }

    void RenderResources::DestroyBuffer(BufferHandle handle)
    {
        if (!handle.IsValid())
            return;
        Record(cmd::DestroyBuffer { handle });
        m_buffers.Free(handle);
    }

    MeshHandle RenderResources::CreateMesh(MeshDataPtr data)
    {
        if (!data)
            return {};
        const auto handle = m_meshes.Allocate();
        Record(cmd::CreateMesh { handle, std::move(data) });
        return handle;
    }

    MeshHandle RenderResources::CreateMesh(const MeshData& data)
    {
        return CreateMesh(MakeShared<const MeshData>(data));
    }

    void RenderResources::DestroyMesh(MeshHandle handle)
    {
        if (!handle.IsValid())
            return;
        Record(cmd::DestroyMesh { handle });
        m_meshes.Free(handle);
    }

    void RenderResources::TakeCommands(ResourceCommandList& out)
    {
        std::lock_guard lock(m_mutex);
        out.Append(m_commands);
    }

    // --- Texture ---

    Texture::Texture(RenderResources& resources, const TextureDesc& desc)
        : m_resources(&resources)
        , m_handle(resources.CreateTexture(desc))
        , m_desc(desc)
    {
    }

    Texture::Texture(Texture&& other) noexcept
        : m_resources(std::exchange(other.m_resources, nullptr))
        , m_handle(std::exchange(other.m_handle, {}))
        , m_desc(std::move(other.m_desc))
    {
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this != &other) {
            Reset();
            m_resources = std::exchange(other.m_resources, nullptr);
            m_handle = std::exchange(other.m_handle, {});
            m_desc = std::move(other.m_desc);
        }
        return *this;
    }

    void Texture::Update(TextureData data)
    {
        if (m_resources)
            m_resources->UpdateTexture(m_handle, std::move(data));
    }

    void Texture::Resize(uint32_t width, uint32_t height)
    {
        if (!m_resources || width == 0 || height == 0 || (width == m_desc.width && height == m_desc.height))
            return;
        m_desc.width = width;
        m_desc.height = height;
        m_resources->ResizeTexture(m_handle, width, height);
    }

    void Texture::Reset()
    {
        if (m_resources && m_handle.IsValid())
            m_resources->DestroyTexture(m_handle);
        m_resources = nullptr;
        m_handle = {};
    }

    // --- Mesh ---

    Mesh::Mesh(RenderResources& resources, MeshDataPtr data)
        : m_resources(&resources)
        , m_handle(resources.CreateMesh(data))
        , m_data(std::move(data))
    {
    }

    Mesh::Mesh(Mesh&& other) noexcept
        : m_resources(std::exchange(other.m_resources, nullptr))
        , m_handle(std::exchange(other.m_handle, {}))
        , m_data(std::move(other.m_data))
    {
    }

    Mesh& Mesh::operator=(Mesh&& other) noexcept
    {
        if (this != &other) {
            Reset();
            m_resources = std::exchange(other.m_resources, nullptr);
            m_handle = std::exchange(other.m_handle, {});
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    void Mesh::Reset()
    {
        if (m_resources && m_handle.IsValid())
            m_resources->DestroyMesh(m_handle);
        m_resources = nullptr;
        m_handle = {};
        m_data.reset();
    }

} // namespace elm::render
