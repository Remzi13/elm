#include "render/backend/vulkan/VulkanCommandTranslator.hpp"

#include "render/backend/vulkan/VulkanFormats.hpp"

#include <algorithm>
#include <cstring>

namespace elm::render::vulkan {

    namespace {

        VkImageLayout ToImageLayout(rhi::ResourceState state)
        {
            switch (state) {
            case rhi::ResourceState::RenderTarget:
                return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case rhi::ResourceState::DepthWrite:
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case rhi::ResourceState::ShaderResource:
            default:
                return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }

        VkRenderingAttachmentInfo AttachmentInfo(VkImageView view, VkImageLayout layout, std::optional<VkClearValue> clear)
        {
            VkRenderingAttachmentInfo info {};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageView = view;
            info.imageLayout = layout;
            info.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (clear)
                info.clearValue = *clear;
            return info;
        }

    } // namespace

    VulkanBackend::CommandTranslator::CommandTranslator(VulkanBackend& backend, VulkanFrame& frame, const FrameStreams& streams,
        const rhi::UploadAllocator& uploads) noexcept
        : m_backend(backend)
        , m_frame(frame)
        , m_streams(streams)
        , m_uploads(uploads)
        , m_commands(frame.GetCommands())
    {
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Render passes
    // ─────────────────────────────────────────────────────────────────────────────

    template <typename Work>
    void VulkanBackend::CommandTranslator::OutsideRendering(Work&& work)
    {
        const bool resume = m_rendering;
        SuspendRendering();
        work();
        if (resume)
            BeginRendering({});
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::Barrier& command)
    {
        auto* gpu = m_backend.FindTexture(command.texture);
        if (!gpu || !gpu->image || gpu->image.GetLayout() == ToImageLayout(command.state))
            return;
        OutsideRendering([&] { gpu->image.Transition(m_commands, ToImageLayout(command.state)); });
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::BeginRenderPass& command)
    {
        Finish();
        auto target = PrepareColorTarget(command.color);
        m_passActive = target.has_value();
        if (!m_passActive)
            return;

        if (auto* depth = m_backend.FindTexture(command.depth.texture); depth && depth->image) {
            depth->image.Transition(m_commands, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            target->depth = depth->image.GetView();
            target->depthHasStencil = depth->desc.format == TextureFormat::D24_UNORM_S8_UINT;
        }
        m_target = *target;

        AttachmentClears clears;
        if (command.color.clear) {
            VkClearColorValue color;
            std::memcpy(color.float32, command.color.clearColor, sizeof(color.float32));
            clears.color = color;
        }
        if (command.depth.clear)
            clears.depth = command.depth.clearDepth;
        BeginRendering(clears);

        SetFullViewport();
        m_scissor = { { 0, 0 }, m_target.extent };
        vkCmdSetScissor(m_commands, 0, 1, &m_scissor);
    }

    std::optional<VulkanBackend::CommandTranslator::RenderTarget> VulkanBackend::CommandTranslator::PrepareColorTarget(
        const rhi::ColorAttachment& color)
    {
        RenderTarget target;
        if (color.useSurface) {
            auto* surface = m_backend.FindSurface(color.surface);
            if (!surface || !surface->Acquire(m_backend.m_frameSlot))
                return std::nullopt;
            RecordImageBarrier(m_commands, surface->GetImage(), surface->GetFormat(), surface->GetLayout(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            surface->SetLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            target.color = surface->GetView();
            target.extent = surface->GetExtent();
            return target;
        }

        auto* gpu = m_backend.FindTexture(color.texture);
        if (!gpu || !gpu->image)
            return std::nullopt;
        gpu->image.Transition(m_commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        target.color = gpu->image.GetView();
        target.extent = { gpu->desc.width, gpu->desc.height };
        return target;
    }

    void VulkanBackend::CommandTranslator::BeginRendering(const AttachmentClears& clears)
    {
        std::optional<VkClearValue> colorClear;
        if (clears.color)
            colorClear = VkClearValue { .color = *clears.color };
        std::optional<VkClearValue> depthClear;
        if (clears.depth)
            depthClear = VkClearValue { .depthStencil = { *clears.depth, 0 } };

        const auto color = AttachmentInfo(m_target.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorClear);
        const auto depth = AttachmentInfo(m_target.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthClear);
        const bool hasDepth = m_target.depth != VK_NULL_HANDLE;

        VkRenderingInfo info {};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = { { 0, 0 }, m_target.extent };
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
        info.pDepthAttachment = hasDepth ? &depth : nullptr;
        info.pStencilAttachment = hasDepth && m_target.depthHasStencil ? &depth : nullptr;
        vkCmdBeginRendering(m_commands, &info);
        m_rendering = true;
    }

    void VulkanBackend::CommandTranslator::SuspendRendering()
    {
        if (!m_rendering)
            return;
        vkCmdEndRendering(m_commands);
        m_rendering = false;
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::EndRenderPass&)
    {
        Finish();
    }

    void VulkanBackend::CommandTranslator::Finish()
    {
        SuspendRendering();
        m_passActive = false;
        m_pipeline = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // State
    // ─────────────────────────────────────────────────────────────────────────────

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::SetPipeline& command)
    {
        m_pipeline = m_backend.m_pipelines.Find(command.pipeline);
        m_texture = {};
        m_fallbackTexture = {};
        m_constants.clear();
        m_descriptorsDirty = true;
        if (!m_pipeline || !m_passActive)
            return;
        vkCmdBindPipeline(m_commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->Get());
        m_constants.resize(m_pipeline->GetSlots().size());
        ApplyScissor();
    }

    void VulkanBackend::CommandTranslator::SetFullViewport()
    {
        Execute(rhi::cmd::SetViewport { 0.0f, 0.0f, static_cast<float>(m_target.extent.width), static_cast<float>(m_target.extent.height) });
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::SetViewport& command)
    {
        if (!m_passActive)
            return;
        // A negative height flips Y, so clip space matches D3D (+Y up) as the shaders expect
        const VkViewport viewport { command.x, command.y + command.height, command.width, -command.height, 0.0f, 1.0f };
        vkCmdSetViewport(m_commands, 0, 1, &viewport);
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::SetScissor& command)
    {
        const int32_t left = std::clamp(command.left, 0, static_cast<int32_t>(m_target.extent.width));
        const int32_t top = std::clamp(command.top, 0, static_cast<int32_t>(m_target.extent.height));
        const int32_t right = std::clamp(command.right, left, static_cast<int32_t>(m_target.extent.width));
        const int32_t bottom = std::clamp(command.bottom, top, static_cast<int32_t>(m_target.extent.height));
        m_scissor = { { left, top }, { static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) } };
        ApplyScissor();
    }

    void VulkanBackend::CommandTranslator::ApplyScissor()
    {
        if (!m_passActive || !m_pipeline)
            return;
        // Pipelines without scissor ignore SetScissor, as with D3D's ScissorEnable
        const VkRect2D full { { 0, 0 }, m_target.extent };
        vkCmdSetScissor(m_commands, 0, 1, m_pipeline->UsesScissor() ? &m_scissor : &full);
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::BindVertexBuffer& command)
    {
        if (command.slot >= MaxVertexSlots)
            return;
        m_vertexBuffers[command.slot] = Resolve(command.source);
        m_vertexBuffersDirty = true;
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::BindIndexBuffer& command)
    {
        m_indexBuffer = Resolve(command.source);
        m_meshIndexCount = 0;
        if (command.source.kind == rhi::BufferSource::Kind::MeshIndices) {
            if (const auto* mesh = m_backend.m_meshes.Find(command.source.mesh))
                m_meshIndexCount = mesh->indexCount;
        }
        m_indexBufferDirty = true;
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::BindTexture& command)
    {
        m_texture = command.texture;
        m_fallbackTexture = command.fallback;
        m_descriptorsDirty = true;
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::SetConstants& command)
    {
        if (!m_pipeline || command.slot >= m_constants.size() || !command.data.IsValid() || command.data.arena != rhi::UploadArena::Constants)
            return;
        const uint32_t size = (std::min)(command.data.size, m_pipeline->GetSlots()[command.slot].size);
        const std::span bytes { m_uploads.GetData(rhi::UploadArena::Constants) + command.data.offset, size };
        m_constants[command.slot] = m_frame.WriteConstants(bytes);
        m_descriptorsDirty = true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Draws
    // ─────────────────────────────────────────────────────────────────────────────

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::DrawIndexed& command)
    {
        if (m_indexBuffer.buffer == VK_NULL_HANDLE || !PrepareDraw())
            return;
        if (m_indexBufferDirty) {
            vkCmdBindIndexBuffer(m_commands, m_indexBuffer.buffer, m_indexBuffer.offset, VK_INDEX_TYPE_UINT32);
            m_indexBufferDirty = false;
        }

        const uint32_t indexCount = command.indexCount != 0 ? command.indexCount : m_meshIndexCount;
        if (indexCount == 0 || command.instanceCount == 0)
            return;
        vkCmdDrawIndexed(m_commands, indexCount, command.instanceCount, command.firstIndex, static_cast<int32_t>(command.baseVertex), 0);
        ++m_drawCount;
    }

    void VulkanBackend::CommandTranslator::Execute(const rhi::cmd::Draw& command)
    {
        if (command.vertexCount == 0 || command.instanceCount == 0 || !PrepareDraw())
            return;
        vkCmdDraw(m_commands, command.vertexCount, command.instanceCount, command.firstVertex, 0);
        ++m_drawCount;
    }

    bool VulkanBackend::CommandTranslator::PrepareDraw()
    {
        if (!m_passActive || !m_pipeline || !PushDescriptors())
            return false;

        const uint32_t count = m_pipeline->GetVertexBufferCount();
        std::array<VkBuffer, MaxVertexSlots> buffers {};
        std::array<VkDeviceSize, MaxVertexSlots> offsets {};
        for (uint32_t slot = 0; slot < count && slot < MaxVertexSlots; ++slot) {
            if (m_vertexBuffers[slot].buffer == VK_NULL_HANDLE)
                return false;
            buffers[slot] = m_vertexBuffers[slot].buffer;
            offsets[slot] = m_vertexBuffers[slot].offset;
        }
        if (m_vertexBuffersDirty && count > 0) {
            vkCmdBindVertexBuffers(m_commands, 0, (std::min)(count, MaxVertexSlots), buffers.data(), offsets.data());
            m_vertexBuffersDirty = false;
        }
        return true;
    }

    bool VulkanBackend::CommandTranslator::PushDescriptors()
    {
        if (!m_descriptorsDirty)
            return true;

        const auto slots = m_pipeline->GetSlots();
        Vector<VkDescriptorBufferInfo> buffers(slots.size());
        Vector<VkDescriptorImageInfo> images(slots.size());
        Vector<VkWriteDescriptorSet> writes;
        for (uint32_t slot = 0; slot < slots.size(); ++slot) {
            VkWriteDescriptorSet write {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = slot;
            write.descriptorCount = 1;
            if (slots[slot].type == BindingType::Constants) {
                if (!m_constants[slot])
                    return false;
                buffers[slot] = { m_constants[slot]->buffer, m_constants[slot]->offset, m_constants[slot]->size };
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &buffers[slot];
            } else {
                auto* texture = ResolveTexture();
                if (!texture)
                    return false;
                images[slot] = { VK_NULL_HANDLE, texture->image.GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write.pImageInfo = &images[slot];
            }
            writes.push_back(write);
        }
        m_backend.m_device.PushDescriptorSet(m_commands, m_pipeline->GetLayout(), writes);
        m_descriptorsDirty = false;
        return true;
    }

    GpuTexture* VulkanBackend::CommandTranslator::ResolveTexture()
    {
        auto* gpu = m_backend.FindTexture(m_texture);
        if (!gpu || !gpu->image)
            gpu = m_backend.FindTexture(m_fallbackTexture);
        if (!gpu || !gpu->image)
            return nullptr;
        if (gpu->image.GetLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            OutsideRendering([&] { gpu->image.Transition(m_commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); });
        return gpu;
    }

    VulkanBackend::CommandTranslator::BoundBuffer VulkanBackend::CommandTranslator::Resolve(const rhi::BufferSource& source) const
    {
        switch (source.kind) {
        case rhi::BufferSource::Kind::MeshVertices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return { mesh->vertexBuffer.Get(), 0 };
            return {};
        case rhi::BufferSource::Kind::MeshIndices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return { mesh->indexBuffer.Get(), 0 };
            return {};
        case rhi::BufferSource::Kind::Buffer:
            if (const auto* buffer = m_backend.m_buffers.Find(source.buffer))
                return { buffer->buffer.Get(), source.offset };
            return {};
        case rhi::BufferSource::Kind::Upload: {
            if (!source.upload.IsValid())
                return {};
            const BufferRange& arena = source.upload.arena == rhi::UploadArena::Index ? m_streams.indices : m_streams.vertices;
            if (source.upload.arena == rhi::UploadArena::Constants || source.upload.offset + source.upload.size > arena.size)
                return {};
            return { arena.buffer, source.upload.offset };
        }
        case rhi::BufferSource::Kind::None:
        default:
            return {};
        }
    }

} // namespace elm::render::vulkan
