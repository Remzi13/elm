#include "render/backend/null/NullBackend.hpp"

#include "core/Log.hpp"

namespace elm::render::null {

class NullBackend::ResourceExecutor {
public:
    explicit ResourceExecutor(NullBackend& backend) noexcept
        : m_backend(backend)
    {
    }

    void Execute(command::resource::CreateTexture& command) { m_backend.m_textures.Insert(command.handle, command.desc); }

    void Execute(command::resource::ResizeTexture& command)
    {
        if (auto* desc = m_backend.m_textures.Find(command.handle)) {
            desc->width = command.width;
            desc->height = command.height;
        }
    }

    void Execute(command::resource::UploadTexture&) { }

    void Execute(command::resource::DestroyTexture& command)
    {
        TextureDesc removed;
        m_backend.m_textures.Remove(command.handle, removed);
    }

    void Execute(command::resource::CreateBuffer&) { }
    void Execute(command::resource::UploadBuffer&) { }
    void Execute(command::resource::DestroyBuffer&) { }

    void Execute(command::resource::CreateMesh& command)
    {
        if (command.data)
            m_backend.m_meshes.Insert(command.handle, static_cast<uint32_t>(command.data->indices.size()));
    }

    void Execute(command::resource::DestroyMesh& command)
    {
        uint32_t removed = 0;
        m_backend.m_meshes.Remove(command.handle, removed);
    }

private:
    NullBackend& m_backend;
};

auto NullBackend::Init([[maybe_unused]] const NativeWindow& window, Size size) -> EngineResult<void>
{
    m_surfaces.insert_or_assign(MainSurface, size);
    WARNING_MESSAGE(log::Category::Render, "NullBackend", "Null render backend: nothing is drawn.");
    return {};
}

void NullBackend::Shutdown()
{
    m_textures.Clear();
    m_meshes.Clear();
    m_transientTextures.Clear();
    m_surfaces.clear();
}

void NullBackend::ExecuteResourceCommands(ResourceCommandList& commands)
{
    ResourceExecutor executor(*this);
    commands.Execute(executor);
    commands.Clear();
}

bool NullBackend::GetTextureDesc(TextureHandle texture, TextureDesc& desc) const
{
    const auto* found = (texture.Index() & TransientIndexBit)
        ? m_transientTextures.Find(TextureHandle(texture.Index() & ~TransientIndexBit, texture.Generation()))
        : m_textures.Find(texture);
    if (!found)
        return false;
    desc = *found;
    return true;
}

TextureHandle NullBackend::CreateTransientTexture(const TextureDesc& desc)
{
    const auto slot = m_transientHandles.Allocate();
    m_transientTextures.Insert(slot, desc);
    return TextureHandle(slot.Index() | TransientIndexBit, slot.Generation());
}

void NullBackend::DestroyTransientTexture(TextureHandle texture)
{
    const TextureHandle slot(texture.Index() & ~TransientIndexBit, texture.Generation());
    TextureDesc removed;
    if (m_transientTextures.Remove(slot, removed))
        m_transientHandles.Free(slot);
}

ShaderHandle NullBackend::CreateShader([[maybe_unused]] const ShaderDesc& desc)
{
    return m_shaders.Allocate();
}

PipelineHandle NullBackend::CreatePipeline(const PipelineDesc& desc)
{
    if (!m_shaders.IsAlive(desc.vertexShader) || !m_shaders.IsAlive(desc.pixelShader))
        return {};
    return m_pipelines.Allocate();
}

void NullBackend::DestroyPipeline(PipelineHandle pipeline)
{
    m_pipelines.Free(pipeline);
}

void NullBackend::CreateSurface(SurfaceId surface, [[maybe_unused]] const NativeWindow& window, uint32_t width, uint32_t height)
{
    m_surfaces.insert_or_assign(surface, Size { width, height });
}

void NullBackend::DestroySurface(SurfaceId surface)
{
    if (surface != MainSurface)
        m_surfaces.erase(surface);
}

void NullBackend::ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height)
{
    if (const auto it = m_surfaces.find(surface); it != m_surfaces.end())
        it->second = { width, height };
}

bool NullBackend::HasSurface(SurfaceId surface) const
{
    return m_surfaces.contains(surface);
}

rhi::SubmitStats NullBackend::Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator&)
{
    rhi::SubmitStats stats;
    for (const auto* list : lists) {
        if (!list)
            continue;
        stats.commands += static_cast<uint32_t>(list->GetCommands().size());
        for (const auto& command : list->GetCommands()) {
            if (std::holds_alternative<rhi::cmd::DrawIndexed>(command))
                ++stats.drawCalls;
        }
    }
    return stats;
}

} // namespace elm::render::null

namespace elm::render::rhi {

UniquePtr<IRenderBackend> createRenderBackend()
{
    return MakeUnique<null::NullBackend>();
}

} // namespace elm::render::rhi
