#include "render/backend/vulkan/VulkanBackend.hpp"

#include "core/Log.hpp"

#include "render/backend/vulkan/VulkanCommandTranslator.hpp"
#include "render/backend/vulkan/VulkanFormats.hpp"

#include <cstring>
#include <utility>

namespace elm::render::vulkan {

namespace {

    constexpr const char* Source = "VulkanBackend";
    /// UploadTexture data is 4 bytes per texel (RGBA8 / BGRA8 / R32).
    constexpr uint32_t TexelSize = 4;

    VkBufferUsageFlags ToBufferUsage(BufferType type)
    {
        switch (type) {
        case BufferType::Vertex:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferType::Index:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferType::Uniform:
        default:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        }
    }

    template <typename T>
    std::span<const uint8_t> AsBytes(const Vector<T>& values)
    {
        // Byte view of trivially copyable geometry for the staging copy
        return { reinterpret_cast<const uint8_t*>(values.data()), values.size() * sizeof(T) };
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Resource commands recorded by RenderResources
// ─────────────────────────────────────────────────────────────────────────────

class VulkanBackend::ResourceExecutor {
public:
    ResourceExecutor(VulkanBackend& backend, VulkanFrame& frame) noexcept
        : m_backend(backend)
        , m_frame(frame)
    {
    }

    void Execute(command::resource::CreateTexture& command)
    {
        if (auto image = m_backend.CreateImage(command.desc))
            m_backend.m_textures.Insert(command.handle, { std::move(image), std::move(command.desc) });
    }

    void Execute(command::resource::ResizeTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (!gpu || (gpu->desc.width == command.width && gpu->desc.height == command.height))
            return;
        auto desc = gpu->desc;
        desc.width = command.width;
        desc.height = command.height;
        if (auto image = m_backend.CreateImage(desc)) {
            m_frame.Retire(std::exchange(gpu->image, std::move(image)));
            gpu->desc = std::move(desc);
        }
    }

    void Execute(command::resource::UploadTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (gpu && gpu->image)
            UploadImage(*gpu, command.data);
    }

    void Execute(command::resource::DestroyTexture& command)
    {
        GpuTexture removed;
        if (m_backend.m_textures.Remove(command.handle, removed))
            m_frame.Retire(std::move(removed.image));
    }

    void Execute(command::resource::CreateBuffer& command)
    {
        const VkBufferUsageFlags usage = ToBufferUsage(command.desc.type) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        auto buffer = VulkanBuffer::Create(m_backend.m_device, { command.desc.size, usage, MemoryKind::DeviceLocal });
        if (!buffer)
            return;
        if (!command.initialData.empty() && command.initialData.size() <= command.desc.size)
            UploadBuffer(buffer.Get(), 0, command.initialData);
        m_backend.m_buffers.Insert(command.handle, { std::move(buffer), std::move(command.desc) });
    }

    void Execute(command::resource::UploadBuffer& command)
    {
        auto* gpu = m_backend.m_buffers.Find(command.handle);
        if (!gpu || !gpu->buffer || command.offset + command.data.size() > gpu->desc.size)
            return;
        UploadBuffer(gpu->buffer.Get(), command.offset, command.data);
    }

    void Execute(command::resource::DestroyBuffer& command)
    {
        GpuBuffer removed;
        if (m_backend.m_buffers.Remove(command.handle, removed))
            m_frame.Retire(std::move(removed.buffer));
    }

    void Execute(command::resource::CreateMesh& command)
    {
        if (!command.data || command.data->vertices.empty() || command.data->indices.empty())
            return;
        GpuMesh mesh;
        mesh.vertexBuffer = CreateFilledBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, AsBytes(command.data->vertices));
        mesh.indexBuffer = CreateFilledBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, AsBytes(command.data->indices));
        mesh.indexCount = static_cast<uint32_t>(command.data->indices.size());
        if (mesh.vertexBuffer && mesh.indexBuffer)
            m_backend.m_meshes.Insert(command.handle, std::move(mesh));
    }

    void Execute(command::resource::DestroyMesh& command)
    {
        GpuMesh removed;
        if (!m_backend.m_meshes.Remove(command.handle, removed))
            return;
        m_frame.Retire(std::move(removed.vertexBuffer));
        m_frame.Retire(std::move(removed.indexBuffer));
    }

private:
    VulkanBuffer CreateFilledBuffer(VkBufferUsageFlags usage, std::span<const uint8_t> bytes)
    {
        auto buffer = VulkanBuffer::Create(m_backend.m_device, { bytes.size(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryKind::DeviceLocal });
        if (buffer)
            UploadBuffer(buffer.Get(), 0, bytes);
        return buffer;
    }

    /// Copy through staging; the barriers order it after earlier reads and before later ones.
    void UploadBuffer(VkBuffer buffer, VkDeviceSize offset, std::span<const uint8_t> bytes)
    {
        const BufferRange staging = m_frame.AllocateStaging(bytes.size());
        if (!staging.data)
            return;
        std::memcpy(staging.data, bytes.data(), bytes.size());

        const VkCommandBuffer commands = m_frame.GetCommands();
        RecordTransferBarrier(commands, TransferBarrier::BeforeCopy);
        const VkBufferCopy region { staging.offset, offset, bytes.size() };
        vkCmdCopyBuffer(commands, staging.buffer, buffer, 1, &region);
        RecordTransferBarrier(commands, TransferBarrier::AfterCopy);
    }

    void UploadImage(GpuTexture& gpu, const TextureData& data)
    {
        const uint32_t width = gpu.desc.width;
        const uint32_t height = gpu.desc.height;
        const size_t rowBytes = static_cast<size_t>(width) * TexelSize;
        const size_t stride = data.stride > 0 ? data.stride : rowBytes;
        if (stride < rowBytes || stride % TexelSize != 0 || data.data.size() < stride * (height - 1) + rowBytes) {
            ERROR_MESSAGE(log::Category::Render, Source, "Texture '%s' upload has the wrong size", gpu.desc.name.c_str());
            return;
        }
        const size_t bytes = stride * (height - 1) + rowBytes;
        const BufferRange staging = m_frame.AllocateStaging(bytes);
        if (!staging.data)
            return;
        std::memcpy(staging.data, data.data.data(), bytes);

        const VkCommandBuffer commands = m_frame.GetCommands();
        gpu.image.Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region {};
        region.bufferOffset = staging.offset;
        region.bufferRowLength = static_cast<uint32_t>(stride / TexelSize);
        region.imageSubresource = { ImageAspect(gpu.desc.format), 0, 0, 1 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(commands, staging.buffer, gpu.image.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        if (gpu.desc.bindFlags & TextureBind::ShaderResource)
            gpu.image.Transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VulkanBackend& m_backend;
    VulkanFrame& m_frame;
};

// ─────────────────────────────────────────────────────────────────────────────
// Backend
// ─────────────────────────────────────────────────────────────────────────────

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend()
{
    Shutdown();
}

auto VulkanBackend::Init(const NativeWindow& window, Size size) -> EngineResult<void>
{
    if (auto result = m_device.Init(window); !result)
        return result;

    m_linearClampSampler = CreateLinearClampSampler();
    for (auto& frame : m_frames) {
        if (auto result = frame.Init(m_device); !result)
            return result;
    }
    return m_mainSurface.Init(m_device, window, { size.width, size.height }, VK_FORMAT_UNDEFINED);
}

void VulkanBackend::Shutdown()
{
    if (!m_device.IsValid())
        return;

    SubmitFrame();
    m_device.WaitIdle();
    m_pipelines.Clear();
    m_shaders.Clear();
    m_transientTextures.Clear();
    m_textures.Clear();
    m_buffers.Clear();
    m_meshes.Clear();
    for (auto& frame : m_frames)
        frame.Shutdown();
    m_surfaces.clear();
    m_mainSurface.Shutdown();
    vkDestroySampler(m_device.Get(), m_linearClampSampler, nullptr);
    m_linearClampSampler = VK_NULL_HANDLE;
    m_device.Shutdown();
}

VkSampler VulkanBackend::CreateLinearClampSampler() const
{
    VkSamplerCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(m_device.Get(), &info, nullptr, &sampler);
    return sampler;
}

VulkanFrame& VulkanBackend::Record()
{
    if (!m_recording) {
        m_frameSlot = (m_frameSlot + 1) % FramesInFlight;
        m_frames[m_frameSlot].Begin();
        m_recording = true;
    }
    return m_frames[m_frameSlot];
}

void VulkanBackend::SubmitFrame()
{
    if (!m_recording)
        return;
    VulkanFrame& frame = m_frames[m_frameSlot];
    const VkCommandBuffer commands = frame.GetCommands();

    Vector<VkSemaphoreSubmitInfo> waits;
    Vector<VkSemaphoreSubmitInfo> signals;
    const auto addSurface = [&](VulkanSwapChain& surface) {
        if (!surface.NeedsSubmit())
            return;
        RecordImageBarrier(commands, surface.GetImage(), surface.GetFormat(), surface.GetLayout(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        surface.SetLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        waits.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, surface.GetImageAvailable(), 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0 });
        signals.push_back({ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, surface.GetRenderFinished(), 0,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0 });
        surface.MarkSubmitted();
    };
    addSurface(m_mainSurface);
    for (auto& [id, surface] : m_surfaces)
        addSurface(*surface);
    vkEndCommandBuffer(commands);

    const VkCommandBufferSubmitInfo commandInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, commands, 0 };
    VkSubmitInfo2 submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
    submit.pWaitSemaphoreInfos = waits.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    submit.pSignalSemaphoreInfos = signals.data();
    if (const VkResult result = vkQueueSubmit2(m_device.GetQueue(), 1, &submit, frame.GetFence()); result != VK_SUCCESS)
        ERROR_MESSAGE(log::Category::Render, Source, "Frame submission failed (VkResult %d)", static_cast<int>(result));
    m_recording = false;
}

void VulkanBackend::ExecuteResourceCommands(ResourceCommandList& commands)
{
    if (commands.Empty())
        return;
    ResourceExecutor executor(*this, Record());
    commands.Execute(executor);
    commands.Clear();
}

GpuTexture* VulkanBackend::FindTexture(TextureHandle handle)
{
    if (handle.Index() & TransientIndexBit)
        return m_transientTextures.Find(TextureHandle(handle.Index() & ~TransientIndexBit, handle.Generation()));
    return m_textures.Find(handle);
}

const GpuTexture* VulkanBackend::FindTexture(TextureHandle handle) const
{
    return const_cast<VulkanBackend*>(this)->FindTexture(handle);
}

bool VulkanBackend::GetTextureDesc(TextureHandle texture, TextureDesc& desc) const
{
    const auto* gpu = FindTexture(texture);
    if (!gpu)
        return false;
    desc = gpu->desc;
    return true;
}

VulkanImage VulkanBackend::CreateImage(const TextureDesc& desc)
{
    const VkFormat format = ToVkFormat(desc.format);
    if (desc.width == 0 || desc.height == 0 || format == VK_FORMAT_UNDEFINED) {
        ERROR_MESSAGE(log::Category::Render, Source, "Texture '%s' has no size or format", desc.name.c_str());
        return {};
    }
    return VulkanImage::Create(m_device, { desc.width, desc.height, format, ToImageUsage(desc.bindFlags), ImageAspect(desc.format) });
}

TextureHandle VulkanBackend::CreateTransientTexture(const TextureDesc& desc)
{
    auto image = CreateImage(desc);
    if (!image)
        return {};
    const auto slot = m_transientHandles.Allocate();
    m_transientTextures.Insert(slot, { std::move(image), desc });
    return TextureHandle(slot.Index() | TransientIndexBit, slot.Generation());
}

void VulkanBackend::DestroyTransientTexture(TextureHandle texture)
{
    if (!(texture.Index() & TransientIndexBit))
        return;
    const TextureHandle slot(texture.Index() & ~TransientIndexBit, texture.Generation());
    GpuTexture removed;
    if (!m_transientTextures.Remove(slot, removed))
        return;
    m_transientHandles.Free(slot);
    CurrentFrame().Retire(std::move(removed.image));
}

ShaderHandle VulkanBackend::CreateShader(const ShaderDesc& desc)
{
    auto shader = LoadSpirvShader(desc);
    if (!shader)
        return {};
    const auto handle = m_shaderHandles.Allocate();
    m_shaders.Insert(handle, std::move(*shader));
    return handle;
}

PipelineHandle VulkanBackend::CreatePipeline(const PipelineDesc& desc)
{
    const auto* vs = m_shaders.Find(desc.vertexShader);
    const auto* ps = m_shaders.Find(desc.pixelShader);
    if (!vs || !ps)
        return {};

    auto pipeline = VulkanPipeline::Create(m_device, { desc, *vs, *ps, m_linearClampSampler });
    if (!pipeline)
        return {};
    const auto handle = m_pipelineHandles.Allocate();
    m_pipelines.Insert(handle, std::move(pipeline));
    return handle;
}

void VulkanBackend::DestroyPipeline(PipelineHandle pipeline)
{
    VulkanPipeline removed;
    if (!m_pipelines.Remove(pipeline, removed))
        return;
    m_pipelineHandles.Free(pipeline);
    CurrentFrame().Retire(std::move(removed));
}

rhi::SubmitStats VulkanBackend::Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads)
{
    VulkanFrame& frame = Record();
    const auto arena = [&](rhi::UploadArena kind) {
        return frame.WriteArena(kind, { uploads.GetData(kind), uploads.GetUsedSize(kind) });
    };
    const FrameStreams streams { arena(rhi::UploadArena::Vertex), arena(rhi::UploadArena::Index) };

    rhi::SubmitStats stats;
    for (const auto* list : lists) {
        if (!list)
            continue;
        // A fresh translator per list: lists are recorded independently and must not inherit state
        CommandTranslator translator(*this, frame, streams, uploads);
        for (const auto& command : list->GetCommands())
            std::visit([&translator](const auto& cmd) { translator.Execute(cmd); }, command);
        translator.Finish();
        stats.drawCalls += translator.GetDrawCount();
        stats.commands += static_cast<uint32_t>(list->GetCommands().size());
    }
    return stats;
}

VulkanSwapChain* VulkanBackend::FindSurface(SurfaceId surface)
{
    if (surface == MainSurface)
        return &m_mainSurface;
    const auto it = m_surfaces.find(surface);
    return it != m_surfaces.end() ? it->second.get() : nullptr;
}

bool VulkanBackend::HasSurface(SurfaceId surface) const
{
    return surface == MainSurface ? m_device.IsValid() : m_surfaces.contains(surface);
}

void VulkanBackend::CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height)
{
    if (surface == MainSurface || width == 0 || height == 0)
        return;
    // Pipelines are built for the main surface format, so every surface uses it
    auto swapChain = MakeUnique<VulkanSwapChain>();
    if (swapChain->Init(m_device, window, { width, height }, m_mainSurface.GetFormat()))
        m_surfaces.insert_or_assign(surface, std::move(swapChain));
}

void VulkanBackend::DestroySurface(SurfaceId surface)
{
    if (surface != MainSurface)
        m_surfaces.erase(surface);
}

void VulkanBackend::ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height)
{
    if (auto* swapChain = FindSurface(surface))
        swapChain->Resize({ width, height });
}

void VulkanBackend::Present(SurfaceId surface)
{
    SubmitFrame();
    if (auto* swapChain = FindSurface(surface))
        swapChain->Present();
}

TextureFormat VulkanBackend::GetSurfaceColorFormat() const
{
    return FromVkFormat(m_mainSurface.GetFormat());
}

TextureFormat VulkanBackend::GetSurfaceDepthFormat() const
{
    return TextureFormat::Unknown;
}

size_t VulkanBackend::GetAllocatedMemory() const
{
    return m_device.GetAllocatedBytes();
}

} // namespace elm::render::vulkan

namespace elm::render::rhi {

UniquePtr<IRenderBackend> createRenderBackend()
{
    return MakeUnique<vulkan::VulkanBackend>();
}

} // namespace elm::render::rhi
