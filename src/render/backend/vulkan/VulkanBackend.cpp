#include "render/backend/vulkan/VulkanBackend.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"

#include "render/ShaderSource.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#if PLATFORM_WIN32
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <wayland-client.h>
#endif

namespace elm::render::vulkan {

namespace {

    constexpr const char* kLogSource = "VulkanBackend";

#define VK_CHECK(expr)                                                                                                 \
    do {                                                                                                               \
        const VkResult _vk_result = (expr);                                                                            \
        if (_vk_result != VK_SUCCESS) {                                                                                \
            ERROR_MESSAGE(log::Category::Render, kLogSource, "%s failed (%d)", #expr, static_cast<int>(_vk_result));   \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

    VkFormat toVkFormat(TextureFormat format)
    {
        switch (format) {
        case TextureFormat::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::BGRA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8_UNORM_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::R32_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::D32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D24_UNORM_S8_UINT:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    TextureFormat fromVkFormat(VkFormat format)
    {
        switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return TextureFormat::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return TextureFormat::RGBA8_UNORM_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return TextureFormat::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return TextureFormat::BGRA8_UNORM_SRGB;
        case VK_FORMAT_R32_SFLOAT:
            return TextureFormat::R32_FLOAT;
        case VK_FORMAT_D32_SFLOAT:
            return TextureFormat::D32_FLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return TextureFormat::D24_UNORM_S8_UINT;
        default:
            return TextureFormat::Unknown;
        }
    }

    VkImageAspectFlags aspectOf(TextureFormat format)
    {
        return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkShaderStageFlagBits toVkStage(ShaderStage stage)
    {
        return stage == ShaderStage::Pixel ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
    }

    uint32_t vertexFormatSize(VertexFormat format)
    {
        switch (format) {
        case VertexFormat::Float2:
            return 8;
        case VertexFormat::Float3:
            return 12;
        case VertexFormat::Float4:
        default:
            return 16;
        }
    }

    VkFormat vertexFormatVk(VertexFormat format)
    {
        switch (format) {
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
        default:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }

    String injectBindings(String source, const PipelineDesc& pipeline)
    {
        for (uint32_t i = 0; i < pipeline.bindings.size(); ++i) {
            const auto& binding = pipeline.bindings[i];
            char attr[128];
            if (binding.type == BindingType::Constants) {
                std::snprintf(attr, sizeof(attr), "[[vk::binding(%u, 0)]] cbuffer %s", i, binding.name.c_str());
                const String needle = String("cbuffer ") + binding.name;
                if (const auto pos = source.find(needle); pos != String::npos)
                    source.replace(pos, needle.size(), attr);
            } else {
                std::snprintf(attr, sizeof(attr),
                    "[[vk::combinedImageSampler]] [[vk::binding(%u, 0)]] Texture2D %s", i, binding.name.c_str());
                const String texNeedle = String("Texture2D ") + binding.name;
                if (const auto pos = source.find(texNeedle); pos != String::npos)
                    source.replace(pos, texNeedle.size(), attr);

                const String samplerName = binding.name + "_sampler";
                char sattr[160];
                std::snprintf(sattr, sizeof(sattr),
                    "[[vk::combinedImageSampler]] [[vk::binding(%u, 0)]] SamplerState %s", i, samplerName.c_str());
                const String sampNeedle = String("SamplerState ") + samplerName;
                if (const auto pos = source.find(sampNeedle); pos != String::npos)
                    source.replace(pos, sampNeedle.size(), sattr);
            }
        }
        return source;
    }

    constexpr const char* kHlslHelpers = R"(
#ifndef _ELM_HLSL_HELPERS_
#define _ELM_HLSL_HELPERS_
float4x4 MatrixFromRows(float4 row0, float4 row1, float4 row2, float4 row3)
{
    return float4x4(row0, row1, row2, row3);
}
float3x3 MatrixFromRows(float3 row0, float3 row1, float3 row2)
{
    return float3x3(row0, row1, row2);
}
#endif
)";

    bool writeTempFile(const std::filesystem::path& path, StringView data)
    {
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(file);
    }

    bool readBinaryFile(const std::filesystem::path& path, Vector<uint32_t>& out)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;
        const auto size = file.tellg();
        if (size <= 0 || size % 4 != 0 || !file.seekg(0))
            return false;
        out.resize(static_cast<size_t>(size) / 4);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Resource commands
// ─────────────────────────────────────────────────────────────────────────────

class VulkanBackend::ResourceExecutor {
public:
    explicit ResourceExecutor(VulkanBackend& backend) noexcept
        : m_backend(backend)
    {
    }

    void Execute(command::resource::CreateTexture& command)
    {
        auto texture = m_backend.CreateNativeTexture(command.desc);
        if (texture.image)
            m_backend.m_textures.Insert(command.handle, std::move(texture));
    }

    void Execute(command::resource::ResizeTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (!gpu || (gpu->desc.width == command.width && gpu->desc.height == command.height))
            return;
        auto desc = gpu->desc;
        desc.width = command.width;
        desc.height = command.height;
        auto texture = m_backend.CreateNativeTexture(desc);
        if (!texture.image)
            return;
        m_backend.DestroyGpuTexture(*gpu);
        *gpu = std::move(texture);
    }

    void Execute(command::resource::UploadTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (!gpu || !gpu->image || command.data.data.empty())
            return;

        const VkDeviceSize imageSize = command.data.data.size();
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        size_t stagingBytes = 0;
        void* mapped = nullptr;
        if (!m_backend.CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem,
                stagingBytes, &mapped))
            return;
        std::memcpy(mapped, command.data.data.data(), command.data.data.size());

        if (!m_backend.BeginFrameCommands()) {
            m_backend.DestroyBuffer(staging, stagingMem, mapped, stagingBytes);
            return;
        }

        m_backend.TransitionImage(m_backend.m_activeCmd, gpu->image, gpu->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            aspectOf(gpu->desc.format));

        VkBufferImageCopy region {};
        region.imageSubresource.aspectMask = aspectOf(gpu->desc.format);
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { gpu->desc.width, gpu->desc.height, 1 };
        vkCmdCopyBufferToImage(m_backend.m_activeCmd, staging, gpu->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
            &region);

        const VkImageLayout finalLayout = (gpu->desc.bindFlags & TextureBind::ShaderResource)
            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_GENERAL;
        m_backend.TransitionImage(m_backend.m_activeCmd, gpu->image, gpu->layout, finalLayout, aspectOf(gpu->desc.format));

        // Flush immediately so staging can be freed (resource uploads happen before Submit)
        m_backend.SubmitFrameCommands();
        vkQueueWaitIdle(m_backend.m_graphicsQueue);
        m_backend.DestroyBuffer(staging, stagingMem, mapped, stagingBytes);
    }

    void Execute(command::resource::DestroyTexture& command)
    {
        GpuTexture removed;
        if (m_backend.m_textures.Remove(command.handle, removed)) {
            m_backend.m_pipelines.ForEach([&](GpuPipeline& pipeline) { pipeline.textureSets.erase(command.handle); });
            m_backend.DestroyGpuTexture(removed);
        }
    }

    void Execute(command::resource::CreateBuffer& command)
    {
        GpuBuffer gpu;
        gpu.desc = command.desc;
        const VkBufferUsageFlags usage = command.desc.type == BufferType::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            : command.desc.type == BufferType::Index                             ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                                                : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        const bool hostVisible = command.desc.usage == ResourceUsage::Dynamic;
        const VkMemoryPropertyFlags memProps = hostVisible
            ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        const VkBufferUsageFlags createUsage = hostVisible ? usage : (usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        if (!m_backend.CreateBuffer(command.desc.size, createUsage, memProps, gpu.buffer, gpu.memory, gpu.memoryBytes,
                hostVisible ? &gpu.mapped : nullptr))
            return;
        if (!command.initialData.empty())
            m_backend.CopyToBuffer(gpu.buffer, command.initialData.data(), command.initialData.size());
        m_backend.m_buffers.Insert(command.handle, std::move(gpu));
    }

    void Execute(command::resource::UploadBuffer& command)
    {
        auto* gpu = m_backend.m_buffers.Find(command.handle);
        if (!gpu || !gpu->buffer || command.offset + command.data.size() > gpu->desc.size)
            return;
        m_backend.CopyToBuffer(gpu->buffer, command.data.data(), command.data.size(), command.offset);
    }

    void Execute(command::resource::DestroyBuffer& command)
    {
        GpuBuffer removed;
        if (m_backend.m_buffers.Remove(command.handle, removed))
            m_backend.DestroyGpuBuffer(removed);
    }

    void Execute(command::resource::CreateMesh& command)
    {
        if (!command.data || command.data->vertices.empty() || command.data->indices.empty())
            return;
        const auto& data = *command.data;
        const VkDeviceSize vertexBytes = data.vertices.size() * sizeof(Vertex);
        const VkDeviceSize indexBytes = data.indices.size() * sizeof(uint32_t);

        GpuMesh mesh;
        if (!m_backend.CreateBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexMemory, mesh.memoryBytes))
            return;
        size_t indexMem = 0;
        if (!m_backend.CreateBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexMemory, indexMem)) {
            m_backend.DestroyGpuMesh(mesh);
            return;
        }
        mesh.memoryBytes += indexMem;
        mesh.indexCount = static_cast<uint32_t>(data.indices.size());
        m_backend.CopyToBuffer(mesh.vertexBuffer, data.vertices.data(), vertexBytes);
        m_backend.CopyToBuffer(mesh.indexBuffer, data.indices.data(), indexBytes);
        m_backend.m_meshes.Insert(command.handle, std::move(mesh));
    }

    void Execute(command::resource::DestroyMesh& command)
    {
        GpuMesh removed;
        if (m_backend.m_meshes.Remove(command.handle, removed))
            m_backend.DestroyGpuMesh(removed);
    }

private:
    VulkanBackend& m_backend;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command list translation
// ─────────────────────────────────────────────────────────────────────────────

class VulkanBackend::CommandTranslator {
public:
    CommandTranslator(VulkanBackend& backend, const rhi::UploadAllocator& uploads) noexcept
        : m_backend(backend)
        , m_cmd(backend.m_activeCmd)
        , m_uploads(uploads)
    {
    }

    void Execute(const rhi::cmd::Barrier& command)
    {
        auto* gpu = m_backend.FindTexture(command.texture);
        if (!gpu || !gpu->image)
            return;
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        switch (command.state) {
        case rhi::ResourceState::RenderTarget:
            layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            break;
        case rhi::ResourceState::DepthWrite:
            layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            break;
        case rhi::ResourceState::ShaderResource:
        default:
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;
        }
        m_backend.TransitionImage(m_cmd, gpu->image, gpu->layout, layout, aspectOf(gpu->desc.format));
    }

    void Execute(const rhi::cmd::BeginRenderPass& command)
    {
        EndRenderingIfNeeded();

        VkImageView colorView = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;

        if (command.color.useSurface) {
            auto* surface = m_backend.FindSurface(command.color.surface);
            if (!surface || !m_backend.AcquireSwapChainImage(*surface))
                return;
            auto& image = surface->images[surface->imageIndex];
            m_backend.TransitionImage(m_cmd, image.image, image.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT);
            colorView = image.view;
            width = surface->width;
            height = surface->height;
            colorFormat = surface->colorFormat;
            m_colorImage = &image.image;
            m_colorLayout = &image.layout;
        } else if (auto* gpu = m_backend.FindTexture(command.color.texture); gpu && gpu->view) {
            m_backend.TransitionImage(m_cmd, gpu->image, gpu->layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                aspectOf(gpu->desc.format));
            colorView = gpu->view;
            width = gpu->desc.width;
            height = gpu->desc.height;
            colorFormat = toVkFormat(gpu->desc.format);
            m_colorTexture = gpu;
        }

        if (auto* gpu = m_backend.FindTexture(command.depth.texture); gpu && gpu->view) {
            m_backend.TransitionImage(m_cmd, gpu->image, gpu->layout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                aspectOf(gpu->desc.format));
            depthView = gpu->view;
            depthFormat = toVkFormat(gpu->desc.format);
            m_depthTexture = gpu;
        }

        if (!colorView || width == 0 || height == 0)
            return;

        m_targetWidth = width;
        m_targetHeight = height;
        m_colorFormat = colorFormat;
        m_depthFormat = depthFormat;

        VkRenderingAttachmentInfo colorAtt { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        colorAtt.imageView = colorView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = command.color.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (command.color.clear) {
            colorAtt.clearValue.color = {{ command.color.clearColor[0], command.color.clearColor[1],
                command.color.clearColor[2], command.color.clearColor[3] }};
        }

        VkRenderingAttachmentInfo depthAtt { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if (depthView) {
            depthAtt.imageView = depthView;
            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthAtt.loadOp = command.depth.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAtt.clearValue.depthStencil = { command.depth.clearDepth, 0 };
        }

        VkRenderingInfo rendering { VK_STRUCTURE_TYPE_RENDERING_INFO };
        rendering.renderArea.extent = { width, height };
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &colorAtt;
        if (depthView)
            rendering.pDepthAttachment = &depthAtt;

        vkCmdBeginRendering(m_cmd, &rendering);
        m_passActive = true;

        const float viewportHeight = static_cast<float>(height);
        const VkViewport viewport { 0.0f, viewportHeight, static_cast<float>(width), -viewportHeight, 0.0f, 1.0f };
        const VkRect2D scissor { { 0, 0 }, { width, height } };
        vkCmdSetViewport(m_cmd, 0, 1, &viewport);
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    void Execute(const rhi::cmd::EndRenderPass&)
    {
        EndRenderingIfNeeded();
        m_pipeline = nullptr;
        m_boundSet = VK_NULL_HANDLE;
    }

    void Execute(const rhi::cmd::SetPipeline& command)
    {
        m_pipeline = m_backend.m_pipelines.Find(command.pipeline);
        if (m_pipeline && m_pipeline->pipeline) {
            vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->pipeline);
            if (m_pipeline->defaultSet && m_pipeline->textures.empty()) {
                vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout, 0, 1,
                    &m_pipeline->defaultSet, 0, nullptr);
                m_boundSet = m_pipeline->defaultSet;
            }
        }
        m_texture = {};
        m_fallbackTexture = {};
    }

    void Execute(const rhi::cmd::SetViewport& command)
    {
        const VkViewport viewport { command.x, command.y + command.height, command.width, -command.height, 0.0f, 1.0f };
        vkCmdSetViewport(m_cmd, 0, 1, &viewport);
    }

    void Execute(const rhi::cmd::SetScissor& command)
    {
        const int32_t left = std::max(command.left, 0);
        const int32_t top = std::max(command.top, 0);
        const int32_t right = std::max(command.right, left);
        const int32_t bottom = std::max(command.bottom, top);
        const VkRect2D scissor { { left, top },
            { static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) } };
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    void Execute(const rhi::cmd::BindVertexBuffer& command)
    {
        if (command.slot >= MaxVertexSlots)
            return;
        VkDeviceSize offset = 0;
        m_vertexBuffers[command.slot] = Resolve(command.source, offset);
        m_vertexOffsets[command.slot] = offset;
        m_vertexSlotCount = std::max(m_vertexSlotCount, command.slot + 1);
        m_vertexBuffersDirty = true;
    }

    void Execute(const rhi::cmd::BindIndexBuffer& command)
    {
        VkDeviceSize offset = 0;
        m_indexBuffer = Resolve(command.source, offset);
        m_indexOffset = offset;
        m_meshIndexCount = 0;
        if (command.source.kind == rhi::BufferSource::Kind::MeshIndices) {
            if (const auto* mesh = m_backend.m_meshes.Find(command.source.mesh))
                m_meshIndexCount = mesh->indexCount;
        }
        m_indexBufferDirty = true;
    }

    void Execute(const rhi::cmd::BindTexture& command)
    {
        m_texture = command.texture;
        m_fallbackTexture = command.fallback;
    }

    void Execute(const rhi::cmd::SetConstants& command)
    {
        if (!m_pipeline || !command.data.IsValid() || command.data.arena != rhi::UploadArena::Constants)
            return;
        for (auto& constants : m_pipeline->constants) {
            if (constants.slot != command.slot || !constants.mapped)
                continue;
            const auto size = std::min(static_cast<uint32_t>(command.data.size), constants.size);
            std::memcpy(constants.mapped, m_uploads.GetData(rhi::UploadArena::Constants) + command.data.offset, size);
        }
    }

    void Execute(const rhi::cmd::DrawIndexed& command)
    {
        if (!m_passActive || !m_pipeline || !m_pipeline->pipeline || !m_indexBuffer)
            return;

        VkDescriptorSet* set = SelectBinding();
        if (!set)
            return;
        if (*set != m_boundSet) {
            vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout, 0, 1, set, 0, nullptr);
            m_boundSet = *set;
        }
        if (m_vertexBuffersDirty) {
            vkCmdBindVertexBuffers(m_cmd, 0, m_vertexSlotCount, m_vertexBuffers, m_vertexOffsets);
            m_vertexBuffersDirty = false;
        }
        if (m_indexBufferDirty) {
            vkCmdBindIndexBuffer(m_cmd, m_indexBuffer, m_indexOffset, VK_INDEX_TYPE_UINT32);
            m_indexBufferDirty = false;
        }

        const uint32_t indexCount = command.indexCount != 0 ? command.indexCount : m_meshIndexCount;
        if (indexCount == 0 || command.instanceCount == 0)
            return;
        vkCmdDrawIndexed(m_cmd, indexCount, command.instanceCount, command.firstIndex, static_cast<int32_t>(command.baseVertex),
            0);
        ++m_drawCount;
    }

    [[nodiscard]] uint32_t GetDrawCount() const noexcept { return m_drawCount; }

    void Finish() { EndRenderingIfNeeded(); }

private:
    static constexpr uint32_t MaxVertexSlots = 4;

    void EndRenderingIfNeeded()
    {
        if (!m_passActive)
            return;
        vkCmdEndRendering(m_cmd);
        m_passActive = false;

        if (m_colorTexture)
            m_colorTexture->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (m_depthTexture)
            m_depthTexture->layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        m_colorTexture = nullptr;
        m_depthTexture = nullptr;
        m_colorImage = nullptr;
        m_colorLayout = nullptr;
    }

    VkBuffer Resolve(const rhi::BufferSource& source, VkDeviceSize& offset)
    {
        offset = 0;
        switch (source.kind) {
        case rhi::BufferSource::Kind::MeshVertices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return mesh->vertexBuffer;
            return VK_NULL_HANDLE;
        case rhi::BufferSource::Kind::MeshIndices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return mesh->indexBuffer;
            return VK_NULL_HANDLE;
        case rhi::BufferSource::Kind::Buffer:
            offset = source.offset;
            if (const auto* buffer = m_backend.m_buffers.Find(source.buffer))
                return buffer->buffer;
            return VK_NULL_HANDLE;
        case rhi::BufferSource::Kind::Upload: {
            const auto arena = static_cast<size_t>(source.upload.arena);
            if (!source.upload.IsValid() || arena >= 2)
                return VK_NULL_HANDLE;
            offset = source.upload.offset;
            return m_backend.m_uploadBuffers[arena].buffer;
        }
        default:
            return VK_NULL_HANDLE;
        }
    }

    VkDescriptorSet* SelectBinding()
    {
        if (m_pipeline->textures.empty())
            return m_pipeline->defaultSet ? &m_pipeline->defaultSet : nullptr;

        TextureHandle handle = m_texture;
        auto* gpu = m_backend.FindTexture(handle);
        if (!gpu || !gpu->view) {
            handle = m_fallbackTexture;
            gpu = m_backend.FindTexture(handle);
        }
        if (!gpu || !gpu->view)
            return nullptr;

        auto& set = m_pipeline->textureSets[handle];
        if (set == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo alloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            alloc.descriptorPool = m_pipeline->descriptorPool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &m_pipeline->setLayout;
            if (vkAllocateDescriptorSets(m_backend.m_device, &alloc, &set) != VK_SUCCESS)
                return nullptr;

            // Bind constant buffers into the set as well
            Vector<VkWriteDescriptorSet> writes;
            Vector<VkDescriptorBufferInfo> bufferInfos;
            bufferInfos.reserve(m_pipeline->constants.size());
            for (const auto& constants : m_pipeline->constants) {
                VkDescriptorBufferInfo info {};
                info.buffer = constants.buffer;
                info.range = constants.size;
                bufferInfos.push_back(info);
                VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                write.dstSet = set;
                write.dstBinding = constants.binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &bufferInfos.back();
                writes.push_back(write);
            }

            VkDescriptorImageInfo imageInfo {};
            imageInfo.imageView = gpu->view;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.sampler = m_pipeline->sampler;
            if (!m_pipeline->textures.empty()) {
                VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                write.dstSet = set;
                write.dstBinding = m_pipeline->textures.front().binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imageInfo;
                writes.push_back(write);
            }
            vkUpdateDescriptorSets(m_backend.m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
        return &set;
    }

    VulkanBackend& m_backend;
    VkCommandBuffer m_cmd;
    const rhi::UploadAllocator& m_uploads;

    bool m_passActive { false };
    uint32_t m_drawCount { 0 };
    uint32_t m_targetWidth { 0 };
    uint32_t m_targetHeight { 0 };
    VkFormat m_colorFormat { VK_FORMAT_UNDEFINED };
    VkFormat m_depthFormat { VK_FORMAT_UNDEFINED };

    GpuPipeline* m_pipeline { nullptr };
    VkDescriptorSet m_boundSet { VK_NULL_HANDLE };
    TextureHandle m_texture;
    TextureHandle m_fallbackTexture;

    GpuTexture* m_colorTexture { nullptr };
    GpuTexture* m_depthTexture { nullptr };
    VkImage* m_colorImage { nullptr };
    VkImageLayout* m_colorLayout { nullptr };

    VkBuffer m_vertexBuffers[MaxVertexSlots] {};
    VkDeviceSize m_vertexOffsets[MaxVertexSlots] {};
    uint32_t m_vertexSlotCount { 0 };
    bool m_vertexBuffersDirty { false };

    VkBuffer m_indexBuffer { VK_NULL_HANDLE };
    VkDeviceSize m_indexOffset { 0 };
    uint32_t m_meshIndexCount { 0 };
    bool m_indexBufferDirty { false };
};

// ─────────────────────────────────────────────────────────────────────────────
// Backend lifecycle
// ─────────────────────────────────────────────────────────────────────────────

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend()
{
    Shutdown();
}

auto VulkanBackend::Init(const NativeWindow& window, Size size) -> EngineResult<void>
{
#ifdef ELM_DXC_PATH
    m_dxcPath = ELM_DXC_PATH;
#else
    m_dxcPath = "dxc";
#endif

    VkApplicationInfo appInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ELM";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    Vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availableExts.data());

    auto isExtSupported = [&](const char* name) {
        return std::any_of(availableExts.begin(), availableExts.end(),
            [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
    };

    Vector<const char*> extensions;
    if (isExtSupported(VK_KHR_SURFACE_EXTENSION_NAME))
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if PLATFORM_WIN32
    if (isExtSupported(VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    if (isExtSupported(VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    if (isExtSupported(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME))
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCI {};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appInfo;
    instanceCI.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCI.ppEnabledExtensionNames = extensions.data();
    if (vkCreateInstance(&instanceCI, nullptr, &m_instance) != VK_SUCCESS)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "vkCreateInstance failed"));

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "No Vulkan devices"));
    Vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    m_physicalDevice = devices[0];
    for (auto device : devices) {
        VkPhysicalDeviceProperties props {};
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = device;
            break;
        }
    }

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    Vector<VkQueueFamilyProperties> queues(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queues.data());
    m_graphicsQueueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsQueueFamily = i;
            break;
        }
    }
    if (m_graphicsQueueFamily == UINT32_MAX)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "No graphics queue"));

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCI { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCI.queueFamilyIndex = m_graphicsQueueFamily;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &priority;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRendering.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &dynamicRendering;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCI { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCI.pNext = &features2;
    deviceCI.queueCreateInfoCount = 1;
    deviceCI.pQueueCreateInfos = &queueCI;
    deviceCI.enabledExtensionCount = 1;
    deviceCI.ppEnabledExtensionNames = deviceExtensions;
    if (vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device) != VK_SUCCESS)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "vkCreateDevice failed"));
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);

    for (auto& frame : m_frames) {
        VkCommandPoolCreateInfo poolCI { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCI.queueFamilyIndex = m_graphicsQueueFamily;
        if (vkCreateCommandPool(m_device, &poolCI, nullptr, &frame.commandPool) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Command pool failed"));

        VkCommandBufferAllocateInfo allocCI { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocCI.commandPool = frame.commandPool;
        allocCI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocCI.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_device, &allocCI, &frame.commandBuffer) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Command buffer failed"));

        VkFenceCreateInfo fenceCI { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(m_device, &fenceCI, nullptr, &frame.inFlight) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Fence failed"));
    }

    auto swap = CreateSwapChain(window, size.width, size.height, true);
    if (!swap)
        return std::unexpected(swap.error());
    m_mainSurface = std::move(*swap);

    for (size_t i = 0; i < 2; ++i) {
        const auto arena = static_cast<rhi::UploadArena>(i);
        const auto capacity = rhi::UploadAllocator::GetDefaultCapacity(arena);
        const VkBufferUsageFlags usage = (arena == rhi::UploadArena::Vertex)
            ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            : VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        m_uploadBuffers[i].desc = { i == 0 ? "Upload Vertex" : "Upload Index",
            i == 0 ? BufferType::Vertex : BufferType::Index, capacity, ResourceUsage::Dynamic };
        if (!CreateBuffer(capacity, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_uploadBuffers[i].buffer, m_uploadBuffers[i].memory, m_uploadBuffers[i].memoryBytes,
                &m_uploadBuffers[i].mapped))
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Upload buffers failed"));
    }

    LOG_MESSAGE(log::Category::Render, kLogSource, "Vulkan backend initialized.");
    return {};
}

void VulkanBackend::Shutdown()
{
    if (!m_device)
        return;
    vkDeviceWaitIdle(m_device);

    m_pipelines.ForEach([this](GpuPipeline& p) { DestroyGpuPipeline(p); });
    m_pipelines.Clear();
    m_shaders.Clear();
    m_transientTextures.ForEach([this](GpuTexture& t) { DestroyGpuTexture(t); });
    m_transientTextures.Clear();
    m_textures.ForEach([this](GpuTexture& t) { DestroyGpuTexture(t); });
    m_textures.Clear();
    m_buffers.ForEach([this](GpuBuffer& b) { DestroyGpuBuffer(b); });
    m_buffers.Clear();
    m_meshes.ForEach([this](GpuMesh& m) { DestroyGpuMesh(m); });
    m_meshes.Clear();

    for (auto& buffer : m_uploadBuffers) {
        DestroyBuffer(buffer.buffer, buffer.memory, buffer.mapped, buffer.memoryBytes);
        buffer = {};
    }

    for (auto& [_, swap] : m_surfaces)
        DestroySwapChain(swap);
    m_surfaces.clear();
    DestroySwapChain(m_mainSurface);

    for (auto& frame : m_frames) {
        if (frame.inFlight)
            vkDestroyFence(m_device, frame.inFlight, nullptr);
        if (frame.commandPool)
            vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
        frame = {};
    }

    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_activeCmd = VK_NULL_HANDLE;
    m_frameOpen = false;
    m_allocatedMemory = 0;
}

void VulkanBackend::ExecuteResourceCommands(ResourceCommandList& commands)
{
    ResourceExecutor executor(*this);
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

TextureHandle VulkanBackend::CreateTransientTexture(const TextureDesc& desc)
{
    auto texture = CreateNativeTexture(desc);
    if (!texture.image)
        return {};
    const auto slot = m_transientHandles.Allocate();
    m_transientTextures.Insert(slot, std::move(texture));
    return TextureHandle(slot.Index() | TransientIndexBit, slot.Generation());
}

void VulkanBackend::DestroyTransientTexture(TextureHandle texture)
{
    if (!(texture.Index() & TransientIndexBit))
        return;
    const TextureHandle slot(texture.Index() & ~TransientIndexBit, texture.Generation());
    GpuTexture removed;
    if (m_transientTextures.Remove(slot, removed)) {
        m_transientHandles.Free(slot);
        m_pipelines.ForEach([&](GpuPipeline& pipeline) { pipeline.textureSets.erase(texture); });
        DestroyGpuTexture(removed);
    }
}

ShaderHandle VulkanBackend::CreateShader(const ShaderDesc& desc)
{
    const String source = loadShaderSource(desc.sourceFile);
    if (source.empty()) {
        ERROR_MESSAGE(log::Category::Render, kLogSource, "Shader source '%s' is missing", desc.sourceFile.c_str());
        return {};
    }
    GpuShader shader;
    shader.desc = desc;
    shader.source = source;
    const auto handle = m_shaderHandles.Allocate();
    m_shaders.Insert(handle, std::move(shader));
    return handle;
}

PipelineHandle VulkanBackend::CreatePipeline(const PipelineDesc& desc)
{
    const auto* vs = m_shaders.Find(desc.vertexShader);
    const auto* ps = m_shaders.Find(desc.pixelShader);
    if (!vs || !ps)
        return {};

    auto vsSpirv = CompileHlsl(ShaderStage::Vertex, desc.name, vs->source, vs->desc.macros, desc);
    auto psSpirv = CompileHlsl(ShaderStage::Pixel, desc.name, ps->source, ps->desc.macros, desc);
    if (!vsSpirv || !psSpirv) {
        ERROR_MESSAGE(log::Category::Render, kLogSource, "Failed to compile pipeline shaders '%s'", desc.name.c_str());
        return {};
    }

    VkShaderModuleCreateInfo vsModCI { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vsModCI.codeSize = vsSpirv->size() * sizeof(uint32_t);
    vsModCI.pCode = vsSpirv->data();
    VkShaderModule vsModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &vsModCI, nullptr, &vsModule) != VK_SUCCESS)
        return {};

    VkShaderModuleCreateInfo psModCI { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    psModCI.codeSize = psSpirv->size() * sizeof(uint32_t);
    psModCI.pCode = psSpirv->data();
    VkShaderModule psModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &psModCI, nullptr, &psModule) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, vsModule, nullptr);
        return {};
    }

    GpuPipeline gpu;
    gpu.colorFormat = toVkFormat(desc.colorFormat);
    gpu.depthFormat = toVkFormat(desc.depthFormat);
    gpu.hasDepth = isDepthFormat(desc.depthFormat);
    gpu.scissor = desc.scissor;

    Vector<VkDescriptorSetLayoutBinding> setBindings;
    for (uint32_t i = 0; i < desc.bindings.size(); ++i) {
        const auto& binding = desc.bindings[i];
        VkDescriptorSetLayoutBinding layoutBinding {};
        layoutBinding.binding = i;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = toVkStage(binding.stage);
        if (binding.type == BindingType::Constants) {
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            GpuPipeline::ConstantSlot slot { i, i, binding.size, {}, {}, nullptr };
            size_t bytes = 0;
            if (!CreateBuffer(binding.size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, slot.buffer, slot.memory,
                    bytes, &slot.mapped)) {
                DestroyGpuPipeline(gpu);
                vkDestroyShaderModule(m_device, vsModule, nullptr);
                vkDestroyShaderModule(m_device, psModule, nullptr);
                return {};
            }
            m_allocatedMemory += bytes;
            gpu.constants.push_back(std::move(slot));
        } else {
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gpu.textures.push_back({ i, i, binding.name });
        }
        setBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo setLayoutCI { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setLayoutCI.bindingCount = static_cast<uint32_t>(setBindings.size());
    setLayoutCI.pBindings = setBindings.data();
    if (vkCreateDescriptorSetLayout(m_device, &setLayoutCI, nullptr, &gpu.setLayout) != VK_SUCCESS) {
        DestroyGpuPipeline(gpu);
        vkDestroyShaderModule(m_device, vsModule, nullptr);
        vkDestroyShaderModule(m_device, psModule, nullptr);
        return {};
    }

    if (!desc.samplers.empty() || !gpu.textures.empty()) {
        VkSamplerCreateInfo samplerCI { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerCI.magFilter = VK_FILTER_LINEAR;
        samplerCI.minFilter = VK_FILTER_LINEAR;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(m_device, &samplerCI, nullptr, &gpu.sampler);
    }

    uint32_t maxSets = 64;
    Vector<VkDescriptorPoolSize> poolSizes;
    if (!gpu.constants.empty())
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * static_cast<uint32_t>(gpu.constants.size()) });
    if (!gpu.textures.empty())
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets });
    if (!poolSizes.empty()) {
        VkDescriptorPoolCreateInfo poolCI { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolCI.maxSets = maxSets;
        poolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolCI.pPoolSizes = poolSizes.data();
        vkCreateDescriptorPool(m_device, &poolCI, nullptr, &gpu.descriptorPool);

        VkDescriptorSetAllocateInfo alloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = gpu.descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &gpu.setLayout;
        if (vkAllocateDescriptorSets(m_device, &alloc, &gpu.defaultSet) == VK_SUCCESS) {
            Vector<VkWriteDescriptorSet> writes;
            Vector<VkDescriptorBufferInfo> bufferInfos;
            for (const auto& constants : gpu.constants) {
                bufferInfos.push_back({ constants.buffer, 0, constants.size });
                VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                write.dstSet = gpu.defaultSet;
                write.dstBinding = constants.binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &bufferInfos.back();
                writes.push_back(write);
            }
            if (!writes.empty())
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    VkPipelineLayoutCreateInfo layoutCI { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutCI.setLayoutCount = gpu.setLayout ? 1u : 0u;
    layoutCI.pSetLayouts = gpu.setLayout ? &gpu.setLayout : nullptr;
    if (vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &gpu.layout) != VK_SUCCESS) {
        DestroyGpuPipeline(gpu);
        vkDestroyShaderModule(m_device, vsModule, nullptr);
        vkDestroyShaderModule(m_device, psModule, nullptr);
        return {};
    }

    VkPipelineShaderStageCreateInfo stages[2] {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = psModule;
    stages[1].pName = "main";

    // Vertex layout: compute strides per buffer slot
    uint32_t strides[8] {};
    for (const auto& attr : desc.vertexLayout)
        strides[attr.bufferSlot] += vertexFormatSize(attr.format);

    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> attributes;
    UnorderedMap<uint32_t, uint32_t> offsets;
    for (const auto& attr : desc.vertexLayout) {
        if (std::none_of(bindings.begin(), bindings.end(), [&](const auto& b) { return b.binding == attr.bufferSlot; })) {
            VkVertexInputBindingDescription binding {};
            binding.binding = attr.bufferSlot;
            binding.stride = strides[attr.bufferSlot];
            binding.inputRate = attr.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            bindings.push_back(binding);
        }
        VkVertexInputAttributeDescription attribute {};
        attribute.location = attr.location;
        attribute.binding = attr.bufferSlot;
        attribute.format = vertexFormatVk(attr.format);
        attribute.offset = offsets[attr.bufferSlot];
        offsets[attr.bufferSlot] += vertexFormatSize(attr.format);
        attributes.push_back(attribute);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
        : desc.cull == CullMode::Front             ? VK_CULL_MODE_FRONT_BIT
                                                  : VK_CULL_MODE_NONE;
    // The negative-height viewport flips framebuffer-space winding.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    if (desc.blend != BlendMode::Opaque) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = desc.blend == BlendMode::AlphaBlend ? VK_BLEND_FACTOR_SRC_ALPHA
                                                                                  : VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingCI { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCI.colorAttachmentCount = 1;
    renderingCI.pColorAttachmentFormats = &gpu.colorFormat;
    if (gpu.hasDepth)
        renderingCI.depthAttachmentFormat = gpu.depthFormat;

    VkGraphicsPipelineCreateInfo pipelineCI { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCI;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &raster;
    pipelineCI.pMultisampleState = &multisample;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &blend;
    pipelineCI.pDynamicState = &dynamic;
    pipelineCI.layout = gpu.layout;

    const VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &gpu.pipeline);
    vkDestroyShaderModule(m_device, vsModule, nullptr);
    vkDestroyShaderModule(m_device, psModule, nullptr);
    if (result != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, kLogSource, "Failed to create pipeline '%s'", desc.name.c_str());
        DestroyGpuPipeline(gpu);
        return {};
    }

    const auto handle = m_pipelineHandles.Allocate();
    m_pipelines.Insert(handle, std::move(gpu));
    return handle;
}

void VulkanBackend::DestroyPipeline(PipelineHandle pipeline)
{
    GpuPipeline removed;
    if (m_pipelines.Remove(pipeline, removed)) {
        m_pipelineHandles.Free(pipeline);
        DestroyGpuPipeline(removed);
    }
}

void VulkanBackend::UploadArena(rhi::UploadArena arena, const rhi::UploadAllocator& uploads)
{
    const auto index = static_cast<size_t>(arena);
    const size_t used = uploads.GetUsedSize(arena);
    if (used == 0 || !m_uploadBuffers[index].mapped)
        return;
    std::memcpy(m_uploadBuffers[index].mapped, uploads.GetData(arena), used);
}

rhi::SubmitStats VulkanBackend::Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads)
{
    rhi::SubmitStats stats;
    if (!BeginFrameCommands())
        return stats;

    m_acquiredThisFrame.clear();
    UploadArena(rhi::UploadArena::Vertex, uploads);
    UploadArena(rhi::UploadArena::Index, uploads);

    for (const auto* list : lists) {
        if (!list)
            continue;
        CommandTranslator translator(*this, uploads);
        for (const auto& command : list->GetCommands())
            std::visit([&translator](const auto& cmd) { translator.Execute(cmd); }, command);
        translator.Finish();
        stats.drawCalls += translator.GetDrawCount();
        stats.commands += static_cast<uint32_t>(list->GetCommands().size());
    }

    // Transition all acquired swapchain images to PRESENT
    for (auto* swap : m_acquiredThisFrame) {
        if (!swap->acquired)
            continue;
        auto& image = swap->images[swap->imageIndex];
        if (image.layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            TransitionImage(m_activeCmd, image.image, image.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_IMAGE_ASPECT_COLOR_BIT);
    }

    SubmitFrameCommands();
    return stats;
}

size_t VulkanBackend::GetAllocatedMemory() const
{
    return m_allocatedMemory;
}

} // namespace elm::render::vulkan

// Remaining methods continue below — split for readability in the same TU.
namespace elm::render::vulkan {

GpuTexture VulkanBackend::CreateNativeTexture(const TextureDesc& desc)
{
    GpuTexture texture;
    texture.desc = desc;
    if (desc.width == 0 || desc.height == 0)
        return texture;

    const VkFormat format = toVkFormat(desc.format);
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (desc.bindFlags & TextureBind::ShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.bindFlags & TextureBind::RenderTarget)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.bindFlags & TextureBind::DepthStencil)
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo imageCI { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent = { desc.width, desc.height, 1 };
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = usage;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &imageCI, nullptr, &texture.image) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, kLogSource, "Failed to create texture '%s'", desc.name.c_str());
        return {};
    }

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(m_device, texture.image, &req);
    VkMemoryAllocateInfo alloc { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &texture.memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, texture.image, nullptr);
        texture.image = VK_NULL_HANDLE;
        return {};
    }
    vkBindImageMemory(m_device, texture.image, texture.memory, 0);
    texture.memoryBytes = static_cast<size_t>(req.size);
    m_allocatedMemory += texture.memoryBytes;

    VkImageViewCreateInfo viewCI { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewCI.image = texture.image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.subresourceRange.aspectMask = aspectOf(desc.format);
    viewCI.subresourceRange.levelCount = 1;
    viewCI.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewCI, nullptr, &texture.view) != VK_SUCCESS) {
        DestroyGpuTexture(texture);
        return {};
    }
    return texture;
}

void VulkanBackend::DestroyGpuTexture(GpuTexture& texture)
{
    if (texture.view)
        vkDestroyImageView(m_device, texture.view, nullptr);
    if (texture.image)
        vkDestroyImage(m_device, texture.image, nullptr);
    if (texture.memory) {
        vkFreeMemory(m_device, texture.memory, nullptr);
        m_allocatedMemory -= texture.memoryBytes;
    }
    texture = {};
}

void VulkanBackend::DestroyGpuBuffer(GpuBuffer& buffer)
{
    DestroyBuffer(buffer.buffer, buffer.memory, buffer.mapped, buffer.memoryBytes);
    buffer = {};
}

void VulkanBackend::DestroyGpuMesh(GpuMesh& mesh)
{
    void* unused = nullptr;
    DestroyBuffer(mesh.vertexBuffer, mesh.vertexMemory, unused, 0);
    DestroyBuffer(mesh.indexBuffer, mesh.indexMemory, unused, 0);
    if (mesh.memoryBytes <= m_allocatedMemory)
        m_allocatedMemory -= mesh.memoryBytes;
    mesh = {};
}

void VulkanBackend::DestroyGpuPipeline(GpuPipeline& pipeline)
{
    if (pipeline.pipeline)
        vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
    if (pipeline.layout)
        vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
    if (pipeline.descriptorPool)
        vkDestroyDescriptorPool(m_device, pipeline.descriptorPool, nullptr);
    if (pipeline.setLayout)
        vkDestroyDescriptorSetLayout(m_device, pipeline.setLayout, nullptr);
    if (pipeline.sampler)
        vkDestroySampler(m_device, pipeline.sampler, nullptr);
    for (auto& constants : pipeline.constants) {
        void* mapped = constants.mapped;
        size_t bytes = constants.size;
        DestroyBuffer(constants.buffer, constants.memory, mapped, bytes);
    }
    pipeline = {};
}

bool VulkanBackend::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    VkBuffer& buffer, VkDeviceMemory& memory, size_t& outBytes, void** mapped)
{
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    outBytes = 0;
    if (mapped)
        *mapped = nullptr;

    VkBufferCreateInfo bufferCI { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCI.size = size;
    bufferCI.usage = usage;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferCI, nullptr, &buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req {};
    vkGetBufferMemoryRequirements(m_device, buffer, &req);
    VkMemoryAllocateInfo alloc { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, properties);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, buffer, memory, 0);
    outBytes = static_cast<size_t>(req.size);
    m_allocatedMemory += outBytes;
    if (mapped && (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        vkMapMemory(m_device, memory, 0, size, 0, mapped);
    return true;
}

void VulkanBackend::DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped, size_t bytes)
{
    if (mapped && memory) {
        vkUnmapMemory(m_device, memory);
        mapped = nullptr;
    }
    if (buffer)
        vkDestroyBuffer(m_device, buffer, nullptr);
    if (memory) {
        vkFreeMemory(m_device, memory, nullptr);
        if (bytes <= m_allocatedMemory)
            m_allocatedMemory -= bytes;
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps {};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return 0;
}

void VulkanBackend::CopyToBuffer(VkBuffer dst, const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    if (!data || size == 0)
        return;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    size_t stagingBytes = 0;
    void* mapped = nullptr;
    if (!CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, stagingMem, stagingBytes,
            &mapped))
        return;
    std::memcpy(mapped, data, static_cast<size_t>(size));

    if (!BeginFrameCommands()) {
        DestroyBuffer(staging, stagingMem, mapped, stagingBytes);
        return;
    }
    VkBufferCopy copy { offset, 0, size };
    // When offset is destination offset:
    copy = { 0, offset, size };
    vkCmdCopyBuffer(m_activeCmd, staging, dst, 1, &copy);
    SubmitFrameCommands();
    vkQueueWaitIdle(m_graphicsQueue);
    DestroyBuffer(staging, stagingMem, mapped, stagingBytes);
}

void VulkanBackend::TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout& layout, VkImageLayout newLayout,
    VkImageAspectFlags aspect)
{
    if (layout == newLayout)
        return;

    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    layout = newLayout;
}

auto VulkanBackend::CreateOsSurface(const NativeWindow& window) -> EngineResult<VkSurfaceKHR>
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#if PLATFORM_WIN32
    VkWin32SurfaceCreateInfoKHR ci { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    ci.hwnd = static_cast<HWND>(window.handle);
    ci.hinstance = GetModuleHandle(nullptr);
    if (vkCreateWin32SurfaceKHR(m_instance, &ci, nullptr, &surface) != VK_SUCCESS)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Win32 surface failed"));
#else
    if (window.platform == NativeWindow::Platform::Wayland) {
        VkWaylandSurfaceCreateInfoKHR ci { VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
        ci.display = static_cast<wl_display*>(window.display);
        ci.surface = static_cast<wl_surface*>(window.handle);
        if (vkCreateWaylandSurfaceKHR(m_instance, &ci, nullptr, &surface) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Wayland surface failed"));
    } else {
        VkXlibSurfaceCreateInfoKHR ci { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
        ci.dpy = static_cast<Display*>(window.display);
        ci.window = static_cast<Window>(reinterpret_cast<uintptr_t>(window.handle));
        if (vkCreateXlibSurfaceKHR(m_instance, &ci, nullptr, &surface) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Xlib surface failed"));
    }
#endif
    return surface;
}

auto VulkanBackend::CreateSwapChain(const NativeWindow& window, uint32_t width, uint32_t height, bool withDepth)
    -> EngineResult<VulkanSwapChain>
{
    VulkanSwapChain swap;
    swap.needsDepth = withDepth;
    auto surface = CreateOsSurface(window);
    if (!surface)
        return std::unexpected(surface.error());
    swap.surface = *surface;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_graphicsQueueFamily, swap.surface, &supported);
    if (!supported)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Surface not supported"));

    VkSurfaceCapabilitiesKHR caps {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, swap.surface, &caps);
    width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width ? caps.maxImageExtent.width : width);
    height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height ? caps.maxImageExtent.height : height);
    if (caps.currentExtent.width != UINT32_MAX) {
        width = caps.currentExtent.width;
        height = caps.currentExtent.height;
    }
    if (width == 0 || height == 0)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Invalid swapchain size"));

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, swap.surface, &formatCount, nullptr);
    Vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, swap.surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = format;
            break;
        }
    }
    swap.colorFormat = chosen.format;
    swap.engineColorFormat = fromVkFormat(chosen.format);
    swap.engineDepthFormat = TextureFormat::D32_FLOAT;
    swap.width = width;
    swap.height = height;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkSwapchainCreateInfoKHR swapCI {};
    swapCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapCI.surface = swap.surface;
    swapCI.minImageCount = imageCount;
    swapCI.imageFormat = chosen.format;
    swapCI.imageColorSpace = chosen.colorSpace;
    swapCI.imageExtent = { width, height };
    swapCI.imageArrayLayers = 1;
    swapCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Do not inherit the display's native rotation here: it makes the rendered image appear
    // upside down on platforms that advertise a rotated surface transform.
    swapCI.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapCI.compositeAlpha = compositeAlpha;
    swapCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapCI.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_device, &swapCI, nullptr, &swap.swapchain) != VK_SUCCESS)
        return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Swapchain create failed"));

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, swap.swapchain, &actualCount, nullptr);
    Vector<VkImage> images(actualCount);
    vkGetSwapchainImagesKHR(m_device, swap.swapchain, &actualCount, images.data());
    swap.images.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        swap.images[i].image = images[i];
        VkImageViewCreateInfo viewCI { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewCI.image = images[i];
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = chosen.format;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &viewCI, nullptr, &swap.images[i].view) != VK_SUCCESS)
            return std::unexpected(EngineError(ErrorCode::RenderEngineInitializationFailed, "Swapchain view failed"));
    }

    if (withDepth) {
        TextureDesc depthDesc;
        depthDesc.name = "SwapchainDepth";
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.format = TextureFormat::D32_FLOAT;
        depthDesc.bindFlags = TextureBind::DepthStencil;
        auto depth = CreateNativeTexture(depthDesc);
        swap.depthImage = depth.image;
        swap.depthMemory = depth.memory;
        swap.depthView = depth.view;
        swap.depthLayout = depth.layout;
        depth = {}; // ownership moved; avoid double-free of memoryBytes accounting by zeroing
        // CreateNativeTexture already counted memory; keep it.
    }

    VkSemaphoreCreateInfo semCI { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore(m_device, &semCI, nullptr, &swap.imageAvailable);
    vkCreateSemaphore(m_device, &semCI, nullptr, &swap.renderFinished);
    return swap;
}

void VulkanBackend::DestroySwapChain(VulkanSwapChain& swap)
{
    if (!m_device)
        return;
    for (auto& image : swap.images) {
        if (image.view)
            vkDestroyImageView(m_device, image.view, nullptr);
    }
    swap.images.clear();
    if (swap.depthView)
        vkDestroyImageView(m_device, swap.depthView, nullptr);
    if (swap.depthImage)
        vkDestroyImage(m_device, swap.depthImage, nullptr);
    if (swap.depthMemory)
        vkFreeMemory(m_device, swap.depthMemory, nullptr);
    if (swap.swapchain)
        vkDestroySwapchainKHR(m_device, swap.swapchain, nullptr);
    if (swap.imageAvailable)
        vkDestroySemaphore(m_device, swap.imageAvailable, nullptr);
    if (swap.renderFinished)
        vkDestroySemaphore(m_device, swap.renderFinished, nullptr);
    if (swap.surface)
        vkDestroySurfaceKHR(m_instance, swap.surface, nullptr);
    swap = {};
}

void VulkanBackend::RecreateSwapChain(VulkanSwapChain& swap, const NativeWindow* window, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    vkDeviceWaitIdle(m_device);
    const bool withDepth = swap.needsDepth;
    NativeWindow native {};
    // Surface recreation requires the original native window; resize keeps the VkSurface.
    VkSurfaceKHR oldSurface = swap.surface;
    swap.surface = VK_NULL_HANDLE; // preserve surface across destroy of swapchain images
    for (auto& image : swap.images) {
        if (image.view)
            vkDestroyImageView(m_device, image.view, nullptr);
    }
    swap.images.clear();
    if (swap.depthView)
        vkDestroyImageView(m_device, swap.depthView, nullptr);
    if (swap.depthImage)
        vkDestroyImage(m_device, swap.depthImage, nullptr);
    if (swap.depthMemory)
        vkFreeMemory(m_device, swap.depthMemory, nullptr);
    swap.depthView = VK_NULL_HANDLE;
    swap.depthImage = VK_NULL_HANDLE;
    swap.depthMemory = VK_NULL_HANDLE;
    if (swap.swapchain)
        vkDestroySwapchainKHR(m_device, swap.swapchain, nullptr);
    swap.swapchain = VK_NULL_HANDLE;
    swap.surface = oldSurface;
    swap.acquired = false;

    VkSurfaceCapabilitiesKHR caps {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, swap.surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) {
        width = caps.currentExtent.width;
        height = caps.currentExtent.height;
    }
    width = std::max(1u, width);
    height = std::max(1u, height);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkSwapchainCreateInfoKHR swapCI {};
    swapCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapCI.surface = swap.surface;
    swapCI.minImageCount = imageCount;
    swapCI.imageFormat = swap.colorFormat;
    swapCI.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapCI.imageExtent = { width, height };
    swapCI.imageArrayLayers = 1;
    swapCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swapCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Do not inherit the display's native rotation here: it makes the rendered image appear
    // upside down on platforms that advertise a rotated surface transform.
    swapCI.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapCI.compositeAlpha = compositeAlpha;
    swapCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapCI.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(m_device, &swapCI, nullptr, &swap.swapchain) != VK_SUCCESS)
        return;
    swap.width = width;
    swap.height = height;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, swap.swapchain, &actualCount, nullptr);
    Vector<VkImage> images(actualCount);
    vkGetSwapchainImagesKHR(m_device, swap.swapchain, &actualCount, images.data());
    swap.images.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        swap.images[i].image = images[i];
        swap.images[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageViewCreateInfo viewCI { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewCI.image = images[i];
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = swap.colorFormat;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.layerCount = 1;
        vkCreateImageView(m_device, &viewCI, nullptr, &swap.images[i].view);
    }

    if (withDepth) {
        TextureDesc depthDesc;
        depthDesc.name = "SwapchainDepth";
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.format = TextureFormat::D32_FLOAT;
        depthDesc.bindFlags = TextureBind::DepthStencil;
        auto depth = CreateNativeTexture(depthDesc);
        swap.depthImage = depth.image;
        swap.depthMemory = depth.memory;
        swap.depthView = depth.view;
        swap.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.image = VK_NULL_HANDLE;
        depth.memory = VK_NULL_HANDLE;
        depth.view = VK_NULL_HANDLE;
    }
    (void)window;
    (void)native;
}

bool VulkanBackend::AcquireSwapChainImage(VulkanSwapChain& swap)
{
    if (swap.acquired)
        return true;
    const VkResult result = vkAcquireNextImageKHR(m_device, swap.swapchain, UINT64_MAX, swap.imageAvailable,
        VK_NULL_HANDLE, &swap.imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        swap.acquired = false;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return false;
    swap.acquired = true;
    if (std::find(m_acquiredThisFrame.begin(), m_acquiredThisFrame.end(), &swap) == m_acquiredThisFrame.end())
        m_acquiredThisFrame.push_back(&swap);
    return true;
}

VulkanSwapChain* VulkanBackend::FindSurface(SurfaceId surface)
{
    if (surface == MainSurface)
        return m_mainSurface ? &m_mainSurface : nullptr;
    const auto it = m_surfaces.find(surface);
    return it != m_surfaces.end() ? &it->second : nullptr;
}

const VulkanSwapChain* VulkanBackend::FindSurface(SurfaceId surface) const
{
    return const_cast<VulkanBackend*>(this)->FindSurface(surface);
}

bool VulkanBackend::HasSurface(SurfaceId surface) const
{
    return FindSurface(surface) != nullptr;
}

void VulkanBackend::CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height)
{
    if (surface == MainSurface || width == 0 || height == 0)
        return;
    auto swap = CreateSwapChain(window, width, height, false);
    if (swap)
        m_surfaces.insert_or_assign(surface, std::move(*swap));
}

void VulkanBackend::DestroySurface(SurfaceId surface)
{
    if (surface == MainSurface)
        return;
    auto it = m_surfaces.find(surface);
    if (it == m_surfaces.end())
        return;
    vkDeviceWaitIdle(m_device);
    DestroySwapChain(it->second);
    m_surfaces.erase(it);
}

void VulkanBackend::ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height)
{
    auto* swap = FindSurface(surface);
    if (!swap || width == 0 || height == 0)
        return;
    if (swap->width == width && swap->height == height)
        return;
    RecreateSwapChain(*swap, nullptr, width, height);
}

void VulkanBackend::Present(SurfaceId surface)
{
    auto* swap = FindSurface(surface);
    if (!swap || !swap->acquired)
        return;

    VkPresentInfoKHR present {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &swap->renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swap->swapchain;
    present.pImageIndices = &swap->imageIndex;
    const VkResult result = vkQueuePresentKHR(m_graphicsQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        RecreateSwapChain(*swap, nullptr, swap->width, swap->height);
    swap->acquired = false;
}

TextureFormat VulkanBackend::GetSurfaceColorFormat() const
{
    return m_mainSurface ? m_mainSurface.engineColorFormat : TextureFormat::Unknown;
}

TextureFormat VulkanBackend::GetSurfaceDepthFormat() const
{
    return m_mainSurface ? m_mainSurface.engineDepthFormat : TextureFormat::Unknown;
}

bool VulkanBackend::BeginFrameCommands()
{
    if (m_frameOpen)
        return true;

    auto& frame = m_frames[m_frameIndex];
    if (vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    vkResetFences(m_device, 1, &frame.inFlight);
    vkResetCommandPool(m_device, frame.commandPool, 0);

    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(frame.commandBuffer, &begin) != VK_SUCCESS)
        return false;

    m_activeCmd = frame.commandBuffer;
    m_frameOpen = true;
    return true;
}

void VulkanBackend::SubmitFrameCommands()
{
    if (!m_frameOpen)
        return;

    auto& frame = m_frames[m_frameIndex];
    vkEndCommandBuffer(m_activeCmd);

    Vector<VkSemaphore> waitSemaphores;
    Vector<VkPipelineStageFlags> waitStages;
    Vector<VkSemaphore> signalSemaphores;
    for (auto* swap : m_acquiredThisFrame) {
        if (!swap->acquired)
            continue;
        waitSemaphores.push_back(swap->imageAvailable);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        signalSemaphores.push_back(swap->renderFinished);
    }

    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submit.pWaitSemaphores = waitSemaphores.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_activeCmd;
    submit.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submit.pSignalSemaphores = signalSemaphores.data();
    vkQueueSubmit(m_graphicsQueue, 1, &submit, frame.inFlight);

    m_frameOpen = false;
    m_activeCmd = VK_NULL_HANDLE;
    frame.submitted = true;
    m_frameIndex = (m_frameIndex + 1) % FramesInFlight;
}

auto VulkanBackend::CompileHlsl(ShaderStage stage, StringView name, StringView source, const Vector<ShaderMacro>& macros,
    const PipelineDesc& pipeline) -> EngineResult<Vector<uint32_t>>
{
    String full = kHlslHelpers;
    for (const auto& macro : macros) {
        full += "#define ";
        full += macro.name;
        full += " ";
        full += macro.value;
        full += "\n";
    }
    // Pipeline macros from CreateShader are already in `macros`; OverlayPass also puts macros on ShaderDesc.
    full += injectBindings(String(source), pipeline);

    namespace fs = std::filesystem;
    const auto tmpDir = fs::temp_directory_path() / "elm_vk_shaders";
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    String safeName = String(name);
    for (char& c : safeName) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            c = '_';
    }
    const auto hlslPath = tmpDir / (safeName + (stage == ShaderStage::Pixel ? ".frag.hlsl" : ".vert.hlsl"));
    const auto spvPath = tmpDir / (safeName + (stage == ShaderStage::Pixel ? ".frag.spv" : ".vert.spv"));
    if (!writeTempFile(hlslPath, full))
        return std::unexpected(EngineError(ErrorCode::ResourceCreationFailed, "Failed to write temp HLSL"));

    const char* profile = stage == ShaderStage::Pixel ? "ps_6_0" : "vs_6_0";
    String cmd = "LD_LIBRARY_PATH=\"";
#ifdef ELM_DXC_LIB_DIR
    cmd += ELM_DXC_LIB_DIR;
#else
    cmd += fs::path(m_dxcPath).parent_path().parent_path() / "lib";
#endif
    cmd += "\" \"";
    cmd += m_dxcPath;
    cmd += "\" -spirv -fspv-target-env=vulkan1.3 -T ";
    cmd += profile;
    cmd += " -E main \"";
    cmd += hlslPath.string();
    cmd += "\" -Fo \"";
    cmd += spvPath.string();
    cmd += "\" 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return std::unexpected(EngineError(ErrorCode::ResourceCreationFailed, "Failed to run dxc"));
    char buffer[512];
    String output;
    while (fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    const int status = pclose(pipe);
    if (status != 0) {
        ERROR_MESSAGE(log::Category::Render, kLogSource, "dxc failed for '%s': %s", String(name).c_str(), output.c_str());
        return std::unexpected(EngineError(ErrorCode::ResourceCreationFailed, "dxc compilation failed"));
    }

    Vector<uint32_t> spirv;
    if (!readBinaryFile(spvPath, spirv))
        return std::unexpected(EngineError(ErrorCode::ResourceCreationFailed, "Failed to read SPIR-V"));
    return spirv;
}

} // namespace elm::render::vulkan

namespace elm::render::rhi {

UniquePtr<IRenderBackend> createRenderBackend()
{
    return MakeUnique<vulkan::VulkanBackend>();
}

} // namespace elm::render::rhi
