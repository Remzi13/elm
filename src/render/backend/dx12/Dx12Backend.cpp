#include "render/backend/dx12/Dx12Backend.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "render/ShaderSource.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace elm::render::dx12 {

namespace {

    constexpr const char* Source = "Dx12Backend";
    constexpr uint32_t TexturePixelSize = 4;
    constexpr uint32_t MinimumUploadAlignment = 16;
    constexpr uint32_t ConstantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

    D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES properties {};
        properties.Type = type;
        properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }

    DXGI_FORMAT ToDxgiFormat(TextureFormat format)
    {
        switch (format) {
        case TextureFormat::RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case TextureFormat::R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::D32_FLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Unknown:
        default:
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    DXGI_FORMAT ToDxgiFormat(VertexFormat format)
    {
        switch (format) {
        case VertexFormat::Float2:
            return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UNorm8x4:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        default:
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    uint32_t VertexFormatSize(VertexFormat format)
    {
        switch (format) {
        case VertexFormat::Float2:
            return 8;
        case VertexFormat::Float3:
            return 12;
        case VertexFormat::Float4:
            return 16;
        case VertexFormat::UNorm8x4:
            return 4;
        default:
            return 0;
        }
    }

    D3D12_RESOURCE_STATES BufferState(BufferType type)
    {
        switch (type) {
        case BufferType::Index:
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case BufferType::Vertex:
        case BufferType::Uniform:
        default:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
    }

    D3D12_SHADER_VISIBILITY ShaderVisibility(ShaderStage stage)
    {
        return stage == ShaderStage::Pixel ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
    }

    String DxilFileName(const ShaderDesc& desc)
    {
        String fileName = desc.sourceFile;
        constexpr StringView HlslExtension = ".hlsl";
        if (fileName.ends_with(HlslExtension))
            fileName.resize(fileName.size() - HlslExtension.size());
        fileName = "dxil/" + fileName;
        for (const auto& macro : desc.macros)
            fileName += "." + macro.name + "_" + macro.value;
        return fileName + ".dxil";
    }

    size_t TextureMemorySize(const TextureDesc& desc)
    {
        return static_cast<size_t>(desc.width) * desc.height * TexturePixelSize;
    }

    uint64_t AlignConstantBufferSize(uint64_t size)
    {
        return (size + ConstantBufferAlignment - 1) & ~(static_cast<uint64_t>(ConstantBufferAlignment) - 1);
    }

    void LogFailure(const char* operation, HRESULT result)
    {
        ERROR_MESSAGE(log::Category::Render, Source, "%s failed (HRESULT 0x%08X)", operation, static_cast<uint32_t>(result));
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor heaps
// ─────────────────────────────────────────────────────────────────────────────

bool Dx12Backend::DescriptorPool::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc {};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    const HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(result)) {
        LogFailure("CreateDescriptorHeap", result);
        return false;
    }
    m_capacity = capacity;
    m_increment = device->GetDescriptorHandleIncrementSize(type);
    return true;
}

uint32_t Dx12Backend::DescriptorPool::Allocate()
{
    if (!m_free.empty()) {
        const uint32_t index = m_free.back();
        m_free.pop_back();
        return index;
    }
    if (m_next == m_capacity)
        return UINT32_MAX;
    return m_next++;
}

void Dx12Backend::DescriptorPool::Free(uint32_t index)
{
    if (index != UINT32_MAX && index < m_next)
        m_free.push_back(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Backend::DescriptorPool::Cpu(uint32_t index) const
{
    auto handle = m_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_increment;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12Backend::DescriptorPool::Gpu(uint32_t index) const
{
    auto handle = m_heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * m_increment;
    return handle;
}

void Dx12Backend::DescriptorPool::Reset()
{
    m_heap.Reset();
    m_free.clear();
    m_capacity = 0;
    m_next = 0;
    m_increment = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource commands
// ─────────────────────────────────────────────────────────────────────────────

class Dx12Backend::ResourceExecutor {
public:
    explicit ResourceExecutor(Dx12Backend& backend) noexcept
        : m_backend(backend)
    {
    }

    void Execute(command::resource::CreateTexture& command)
    {
        auto texture = m_backend.CreateTexture(command.desc);
        if (!texture.resource)
            return;
        m_backend.m_allocatedMemory += texture.memoryBytes;
        m_backend.m_textures.Insert(command.handle, std::move(texture));
    }

    void Execute(command::resource::ResizeTexture& command)
    {
        auto* current = m_backend.m_textures.Find(command.handle);
        if (!current || (current->desc.width == command.width && current->desc.height == command.height))
            return;
        TextureDesc desc = current->desc;
        desc.width = command.width;
        desc.height = command.height;
        auto replacement = m_backend.CreateTexture(desc);
        if (!replacement.resource)
            return;
        m_backend.m_allocatedMemory += replacement.memoryBytes;
        GpuTexture old;
        m_backend.m_textures.Remove(command.handle, old);
        m_backend.Retire(std::move(old));
        m_backend.m_textures.Insert(command.handle, std::move(replacement));
    }

    void Execute(command::resource::UploadTexture& command)
    {
        if (auto* texture = m_backend.m_textures.Find(command.handle))
            m_backend.UploadTexture(*texture, command.data);
    }

    void Execute(command::resource::DestroyTexture& command)
    {
        GpuTexture removed;
        if (m_backend.m_textures.Remove(command.handle, removed))
            m_backend.Retire(std::move(removed));
    }

    void Execute(command::resource::CreateBuffer& command)
    {
        auto buffer = m_backend.CreateBuffer(command.desc, BufferState(command.desc.type));
        if (!buffer.resource)
            return;
        if (!command.initialData.empty()) {
            if (command.initialData.size() > command.desc.size) {
                ERROR_MESSAGE(log::Category::Render, Source, "Initial data is larger than buffer '%s'", command.desc.name.c_str());
                return;
            }
            m_backend.UploadBuffer(buffer, 0, command.initialData);
        }
        m_backend.m_allocatedMemory += buffer.memoryBytes;
        m_backend.m_buffers.Insert(command.handle, std::move(buffer));
    }

    void Execute(command::resource::UploadBuffer& command)
    {
        auto* buffer = m_backend.m_buffers.Find(command.handle);
        if (!buffer || command.offset > buffer->desc.size || command.data.size() > buffer->desc.size - command.offset) {
            ERROR_MESSAGE(log::Category::Render, Source, "Buffer upload is outside the resource bounds");
            return;
        }
        m_backend.UploadBuffer(*buffer, command.offset, command.data);
    }

    void Execute(command::resource::DestroyBuffer& command)
    {
        GpuBuffer removed;
        if (m_backend.m_buffers.Remove(command.handle, removed))
            m_backend.Retire(std::move(removed));
    }

    void Execute(command::resource::CreateMesh& command)
    {
        if (!command.data || command.data->vertices.empty() || command.data->indices.empty())
            return;
        const auto vertexBytes = std::span<const uint8_t> {
            reinterpret_cast<const uint8_t*>(command.data->vertices.data()),
            command.data->vertices.size() * sizeof(command.data->vertices.front())
        };
        const auto indexBytes = std::span<const uint8_t> {
            reinterpret_cast<const uint8_t*>(command.data->indices.data()),
            command.data->indices.size() * sizeof(uint32_t)
        };
        GpuMesh mesh;
        mesh.vertices = m_backend.CreateBuffer({ "Mesh vertices", BufferType::Vertex, vertexBytes.size(), ResourceUsage::Default },
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        mesh.indices = m_backend.CreateBuffer({ "Mesh indices", BufferType::Index, indexBytes.size(), ResourceUsage::Default },
            D3D12_RESOURCE_STATE_INDEX_BUFFER);
        if (!mesh.vertices.resource || !mesh.indices.resource)
            return;
        m_backend.UploadBuffer(mesh.vertices, 0, vertexBytes);
        m_backend.UploadBuffer(mesh.indices, 0, indexBytes);
        mesh.indexCount = static_cast<uint32_t>(command.data->indices.size());
        mesh.memoryBytes = mesh.vertices.memoryBytes + mesh.indices.memoryBytes;
        m_backend.m_allocatedMemory += mesh.memoryBytes;
        m_backend.m_meshes.Insert(command.handle, std::move(mesh));
    }

    void Execute(command::resource::DestroyMesh& command)
    {
        GpuMesh removed;
        if (m_backend.m_meshes.Remove(command.handle, removed))
            m_backend.Retire(std::move(removed));
    }

private:
    Dx12Backend& m_backend;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command translation
// ─────────────────────────────────────────────────────────────────────────────

class Dx12Backend::CommandTranslator {
public:
    CommandTranslator(Dx12Backend& backend, const rhi::UploadAllocator& uploads) noexcept
        : m_backend(backend)
        , m_uploads(uploads)
        , m_commands(backend.m_commandList.Get())
    {
    }

    void Execute(const rhi::cmd::Barrier& command)
    {
        auto* texture = m_backend.FindTexture(command.texture);
        if (!texture || !texture->resource)
            return;
        D3D12_RESOURCE_STATES state = static_cast<D3D12_RESOURCE_STATES>(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (command.state == rhi::ResourceState::RenderTarget)
            state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        else if (command.state == rhi::ResourceState::DepthWrite)
            state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        SuspendRendering();
        m_backend.Transition(texture->resource.Get(), texture->state, state);
        ResumeRendering();
    }

    void Execute(const rhi::cmd::BeginRenderPass& command)
    {
        Finish();
        m_color = {};
        m_depth = {};
        if (command.color.useSurface) {
            auto* surface = m_backend.FindSurface(command.color.surface);
            if (!surface || !surface->swapChain)
                return;
            surface->bufferIndex = surface->swapChain->GetCurrentBackBufferIndex();
            if (surface->bufferIndex >= surface->buffers.size())
                return;
            auto& currentState = surface->states[surface->bufferIndex];
            m_backend.Transition(surface->buffers[surface->bufferIndex].Get(), currentState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_color = m_backend.CurrentRenderTarget(*surface);
            m_targetWidth = surface->width;
            m_targetHeight = surface->height;
        } else {
            auto* texture = m_backend.FindTexture(command.color.texture);
            if (!texture || !texture->resource || texture->rtvIndex == InvalidDescriptor)
                return;
            m_backend.Transition(texture->resource.Get(), texture->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_color = m_backend.m_rtvDescriptors.Cpu(texture->rtvIndex);
            m_targetWidth = texture->desc.width;
            m_targetHeight = texture->desc.height;
        }

        if (auto* depth = m_backend.FindTexture(command.depth.texture);
            depth && depth->resource && depth->dsvIndex != InvalidDescriptor) {
            m_backend.Transition(depth->resource.Get(), depth->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            m_depth = m_backend.m_dsvDescriptors.Cpu(depth->dsvIndex);
        }
        m_passActive = true;
        BindTargets();
        if (command.color.clear)
            m_commands->ClearRenderTargetView(m_color, command.color.clearColor, 0, nullptr);
        if (m_depth.ptr != 0 && command.depth.clear)
            m_commands->ClearDepthStencilView(m_depth, D3D12_CLEAR_FLAG_DEPTH, command.depth.clearDepth, 0, 0, nullptr);
        SetViewport({ 0.0f, 0.0f, static_cast<float>(m_targetWidth), static_cast<float>(m_targetHeight), 0.0f, 1.0f });
        m_scissor = { 0, 0, static_cast<LONG>(m_targetWidth), static_cast<LONG>(m_targetHeight) };
        m_commands->RSSetScissorRects(1, &m_scissor);
    }

    void Execute(const rhi::cmd::EndRenderPass&)
    {
        Finish();
    }

    void Execute(const rhi::cmd::SetPipeline& command)
    {
        m_pipeline = m_backend.m_pipelines.Find(command.pipeline);
        m_texture = {};
        m_fallbackTexture = {};
        // Without a pass the root signature is not set: later root arguments must not reach the command list
        if (!m_pipeline || !m_pipeline->state || !m_passActive) {
            m_pipeline = nullptr;
            return;
        }
        m_commands->SetPipelineState(m_pipeline->state.Get());
        m_commands->SetGraphicsRootSignature(m_pipeline->rootSignature.Get());
        m_commands->IASetPrimitiveTopology(m_pipeline->topology);
        for (uint32_t slot = 0; slot < m_vertexSlotCount && slot < m_pipeline->vertexStrides.size(); ++slot) {
            if (m_vertexBuffers[slot].BufferLocation != 0)
                m_vertexBuffers[slot].StrideInBytes = m_pipeline->vertexStrides[slot];
        }
        m_vertexBuffersDirty = true;
        if (m_pipeline->scissor)
            m_commands->RSSetScissorRects(1, &m_scissor);
    }

    void Execute(const rhi::cmd::SetViewport& command)
    {
        SetViewport({ command.x, command.y, command.width, command.height, 0.0f, 1.0f });
    }

    void Execute(const rhi::cmd::SetScissor& command)
    {
        m_scissor.left = std::clamp(static_cast<LONG>(command.left), 0L, static_cast<LONG>(m_targetWidth));
        m_scissor.top = std::clamp(static_cast<LONG>(command.top), 0L, static_cast<LONG>(m_targetHeight));
        m_scissor.right = std::clamp(static_cast<LONG>(command.right), m_scissor.left, static_cast<LONG>(m_targetWidth));
        m_scissor.bottom = std::clamp(static_cast<LONG>(command.bottom), m_scissor.top, static_cast<LONG>(m_targetHeight));
        if (m_pipeline && m_pipeline->scissor)
            m_commands->RSSetScissorRects(1, &m_scissor);
    }

    void Execute(const rhi::cmd::BindVertexBuffer& command)
    {
        if (command.slot >= MaxVertexSlots)
            return;
        const auto resolved = Resolve(command.source);
        m_vertexBuffers[command.slot] = {};
        if (resolved.resource && resolved.size > resolved.offset) {
            const uint32_t stride = m_pipeline && command.slot < m_pipeline->vertexStrides.size()
                ? m_pipeline->vertexStrides[command.slot] : 0;
            if (stride == 0)
                return;
            m_vertexBuffers[command.slot] = {
                resolved.resource->GetGPUVirtualAddress() + resolved.offset,
                static_cast<UINT>(resolved.size - resolved.offset),
                stride
            };
        }
        m_vertexSlotCount = (std::max)(m_vertexSlotCount, command.slot + 1);
        m_vertexBuffersDirty = true;
    }

    void Execute(const rhi::cmd::BindIndexBuffer& command)
    {
        const auto resolved = Resolve(command.source);
        m_indexBuffer = {};
        if (resolved.resource && resolved.size > resolved.offset) {
            m_indexBuffer.BufferLocation = resolved.resource->GetGPUVirtualAddress() + resolved.offset;
            m_indexBuffer.SizeInBytes = static_cast<UINT>(resolved.size - resolved.offset);
            m_indexBuffer.Format = DXGI_FORMAT_R32_UINT;
        }
        m_meshIndexCount = command.source.kind == rhi::BufferSource::Kind::MeshIndices
            ? MeshIndexCount(command.source.mesh)
            : 0;
    }

    void Execute(const rhi::cmd::BindTexture& command)
    {
        if (!m_pipeline || command.slot != m_pipeline->textureBindingSlot)
            return;
        m_texture = command.texture;
        m_fallbackTexture = command.fallback;
    }

    void Execute(const rhi::cmd::SetConstants& command)
    {
        if (!m_pipeline || !command.data.IsValid() || command.data.arena != rhi::UploadArena::Constants)
            return;
        if (command.data.offset > m_uploads.GetUsedSize(command.data.arena) ||
            command.data.size > m_uploads.GetUsedSize(command.data.arena) - command.data.offset) {
            ERROR_MESSAGE(log::Category::Render, Source, "Constant upload is outside the frame arena");
            return;
        }
        for (auto& constants : m_pipeline->constants) {
            if (constants.slot != command.slot)
                continue;
            FrameContext& frame = m_backend.CurrentFrame();
            const uint64_t allocationSize = AlignConstantBufferSize((std::max)(constants.size, command.data.size));
            if (frame.constantsOffset > m_backend.m_constantUploadCapacity ||
                allocationSize > m_backend.m_constantUploadCapacity - frame.constantsOffset) {
                ERROR_MESSAGE(log::Category::Render, Source, "Per-frame constant upload buffer is full");
                return;
            }
            auto* destination = frame.constantsMapped + frame.constantsOffset;
            std::memset(destination, 0, static_cast<size_t>(allocationSize));
            std::memcpy(destination, m_uploads.GetData(command.data.arena) + command.data.offset, command.data.size);
            m_commands->SetGraphicsRootConstantBufferView(constants.rootParameter,
                frame.constants->GetGPUVirtualAddress() + frame.constantsOffset);
            frame.constantsOffset += allocationSize;
            return;
        }
    }

    void Execute(const rhi::cmd::DrawIndexed& command)
    {
        if (m_indexBuffer.BufferLocation == 0 || !PrepareDraw())
            return;
        m_commands->IASetIndexBuffer(&m_indexBuffer);
        const uint32_t count = command.indexCount != 0 ? command.indexCount : m_meshIndexCount;
        if (count == 0 || command.instanceCount == 0)
            return;
        m_commands->DrawIndexedInstanced(count, command.instanceCount, command.firstIndex, command.baseVertex, 0);
        ++m_drawCount;
    }

    void Execute(const rhi::cmd::Draw& command)
    {
        if (command.vertexCount == 0 || command.instanceCount == 0 || !PrepareDraw())
            return;
        m_commands->DrawInstanced(command.vertexCount, command.instanceCount, command.firstVertex, 0);
        ++m_drawCount;
    }

    void Finish()
    {
        if (m_passActive)
            m_commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        m_passActive = false;
        m_pipeline = nullptr;
        m_color = {};
        m_depth = {};
    }

    [[nodiscard]] uint32_t GetDrawCount() const noexcept { return m_drawCount; }

private:
    struct ResolvedBuffer {
        ID3D12Resource* resource { nullptr };
        uint64_t size { 0 };
        uint64_t offset { 0 };
    };

    [[nodiscard]] ResolvedBuffer Resolve(const rhi::BufferSource& source) const
    {
        switch (source.kind) {
        case rhi::BufferSource::Kind::MeshVertices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return { mesh->vertices.resource.Get(), mesh->vertices.desc.size, 0 };
            break;
        case rhi::BufferSource::Kind::MeshIndices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return { mesh->indices.resource.Get(), mesh->indices.desc.size, 0 };
            break;
        case rhi::BufferSource::Kind::Buffer:
            if (const auto* buffer = m_backend.m_buffers.Find(source.buffer))
                return { buffer->resource.Get(), buffer->desc.size, source.offset };
            break;
        case rhi::BufferSource::Kind::Upload: {
            const size_t arena = static_cast<size_t>(source.upload.arena);
            if (source.upload.IsValid() && arena < StreamArenaCount)
                return { m_backend.CurrentFrame().streams[arena].Get(), m_uploads.GetCapacity(source.upload.arena), source.upload.offset };
            break;
        }
        case rhi::BufferSource::Kind::None:
        default:
            break;
        }
        return {};
    }

    [[nodiscard]] uint32_t MeshIndexCount(MeshHandle mesh) const
    {
        const auto* gpu = m_backend.m_meshes.Find(mesh);
        return gpu ? gpu->indexCount : 0;
    }

    void SetViewport(const D3D12_VIEWPORT& viewport)
    {
        if (!m_passActive)
            return;
        m_commands->RSSetViewports(1, &viewport);
    }

    void BindTargets()
    {
        if (!m_passActive)
            return;
        m_commands->OMSetRenderTargets(1, &m_color, m_depth.ptr != 0, m_depth.ptr != 0 ? &m_depth : nullptr);
    }

    void SuspendRendering()
    {
        if (m_passActive)
            m_commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    }

    void ResumeRendering()
    {
        BindTargets();
    }

    [[nodiscard]] bool PrepareDraw()
    {
        if (!m_passActive || !m_pipeline || !m_pipeline->state)
            return false;
        if (m_pipeline->vertexBufferCount > m_vertexSlotCount)
            return false;
        for (uint32_t slot = 0; slot < m_pipeline->vertexBufferCount; ++slot) {
            if (m_vertexBuffers[slot].BufferLocation == 0)
                return false;
        }
        if (m_vertexBuffersDirty) {
            m_commands->IASetVertexBuffers(0, m_vertexSlotCount, m_vertexBuffers.data());
            m_vertexBuffersDirty = false;
        }
        if (m_pipeline->textureBindingSlot != UINT32_MAX) {
            auto* texture = m_backend.FindTexture(m_texture);
            if (!texture || texture->srvIndex == InvalidDescriptor)
                texture = m_backend.FindTexture(m_fallbackTexture);
            if (!texture || texture->srvIndex == InvalidDescriptor)
                return false;
            const uint32_t descriptor = m_backend.m_shaderDescriptors.Allocate();
            if (descriptor == InvalidDescriptor) {
                ERROR_MESSAGE(log::Category::Render, Source, "Per-frame shader descriptor heap is full");
                return false;
            }
            m_backend.CurrentFrame().shaderDescriptors.push_back(descriptor);
            m_backend.m_device->CopyDescriptorsSimple(1, m_backend.m_shaderDescriptors.Cpu(descriptor),
                m_backend.m_srvDescriptors.Cpu(texture->srvIndex), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12DescriptorHeap* heaps[] { m_backend.m_shaderDescriptors.GetHeap() };
            m_commands->SetDescriptorHeaps(1, heaps);
            m_commands->SetGraphicsRootDescriptorTable(m_pipeline->textureRootParameter,
                m_backend.m_shaderDescriptors.Gpu(descriptor));
        }
        return true;
    }

    Dx12Backend& m_backend;
    const rhi::UploadAllocator& m_uploads;
    ID3D12GraphicsCommandList* m_commands { nullptr };
    GpuPipeline* m_pipeline { nullptr };
    TextureHandle m_texture;
    TextureHandle m_fallbackTexture;
    std::array<D3D12_VERTEX_BUFFER_VIEW, MaxVertexSlots> m_vertexBuffers {};
    D3D12_INDEX_BUFFER_VIEW m_indexBuffer {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_color {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_depth {};
    D3D12_RECT m_scissor {};
    uint32_t m_vertexSlotCount { 0 };
    uint32_t m_meshIndexCount { 0 };
    uint32_t m_targetWidth { 0 };
    uint32_t m_targetHeight { 0 };
    uint32_t m_drawCount { 0 };
    bool m_vertexBuffersDirty { false };
    bool m_passActive { false };
};

// ─────────────────────────────────────────────────────────────────────────────
// Device and command queue
// ─────────────────────────────────────────────────────────────────────────────

Dx12Backend::~Dx12Backend()
{
    Shutdown();
}

auto Dx12Backend::Init(const NativeWindow& window, Size size) -> EngineResult<void>
{
    if (!window.IsValid() || window.system != WindowSystem::Win32) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Direct3D 12 requires a valid Win32 window",
            log::Category::Render, Source);
    }

    HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory));
    if (FAILED(result))
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create DXGI factory",
            log::Category::Render, Source);

    for (UINT index = 0; m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&m_adapter)) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc {};
        if (FAILED(m_adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;
        result = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
        if (SUCCEEDED(result))
            break;
        m_adapter.Reset();
    }
    if (!m_device) {
        result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
        if (FAILED(result)) {
            LogFailure("D3D12CreateDevice", result);
            return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a Direct3D 12 device",
                log::Category::Render, Source);
        }
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    if (FAILED(result)) {
        LogFailure("CreateCommandQueue", result);
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a Direct3D 12 command queue",
            log::Category::Render, Source);
    }

    m_constantUploadCapacity = rhi::UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Constants)
        * ConstantBufferAlignment / MinimumUploadAlignment;
    for (auto& frame : m_frames) {
        if (auto frameResult = InitFrame(frame); !frameResult)
            return frameResult;
    }
    result = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames.front().allocator.Get(), nullptr,
        IID_PPV_ARGS(&m_commandList));
    if (FAILED(result)) {
        LogFailure("CreateCommandList", result);
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a graphics command list",
            log::Category::Render, Source);
    }
    m_commandList->Close();

    result = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(result)) {
        LogFailure("CreateFence", result);
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a GPU fence",
            log::Category::Render, Source);
    }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a GPU fence event",
            log::Category::Render, Source);
    }

    if (!m_rtvDescriptors.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RenderTargetDescriptorCapacity, false) ||
        !m_dsvDescriptors.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DepthDescriptorCapacity, false) ||
        !m_srvDescriptors.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ShaderResourceDescriptorCapacity, false) ||
        !m_shaderDescriptors.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ShaderDescriptorCapacity, true)) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create Direct3D 12 descriptor heaps",
            log::Category::Render, Source);
    }

    m_mainSurface.width = size.width;
    m_mainSurface.height = size.height;
    if (!CreateSurfaceResources(m_mainSurface, window, size.width, size.height)) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create the main DXGI swap chain",
            log::Category::Render, Source);
    }
    return {};
}

void Dx12Backend::Shutdown()
{
    if (m_recording) {
        m_commandList->Close();
        m_recording = false;
    }
    WaitForIdle();
    m_pipelines.Clear();
    m_shaders.Clear();
    m_transientTextures.Clear();
    m_textures.Clear();
    m_buffers.Clear();
    m_meshes.Clear();
    for (auto& frame : m_frames)
        ShutdownFrame(frame);
    m_frameSlot = 0;
    m_constantUploadCapacity = 0;
    m_surfaces.clear();
    ReleaseSurfaceResources(m_mainSurface);
    m_mainSurface = {};
    m_shaderDescriptors.Reset();
    m_srvDescriptors.Reset();
    m_dsvDescriptors.Reset();
    m_rtvDescriptors.Reset();
    m_commandList.Reset();
    m_fence.Reset();
    m_fenceValue = 0;
    m_queue.Reset();
    m_device.Reset();
    m_factory.Reset();
    m_adapter.Reset();
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    m_allocatedMemory = 0;
}

auto Dx12Backend::InitFrame(FrameContext& frame) -> EngineResult<void>
{
    const HRESULT result = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator));
    if (FAILED(result)) {
        LogFailure("CreateCommandAllocator", result);
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create a command allocator",
            log::Category::Render, Source);
    }

    const auto createMapped = [&](uint64_t capacity, ComPtr<ID3D12Resource>& buffer, uint8_t*& mapped) {
        buffer = CreateCommittedBuffer(capacity, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* pointer = nullptr;
        const D3D12_RANGE readRange { 0, 0 };
        if (!buffer || FAILED(buffer->Map(0, &readRange, &pointer)))
            return false;
        mapped = static_cast<uint8_t*>(pointer);
        m_allocatedMemory += static_cast<size_t>(capacity);
        return true;
    };
    for (uint32_t index = 0; index < StreamArenaCount; ++index) {
        const uint64_t capacity = rhi::UploadAllocator::GetDefaultCapacity(static_cast<rhi::UploadArena>(index));
        if (!createMapped(capacity, frame.streams[index], frame.streamsMapped[index])) {
            return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create upload stream buffers",
                log::Category::Render, Source);
        }
    }
    if (!createMapped(m_constantUploadCapacity, frame.constants, frame.constantsMapped)) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create the constant upload buffer",
            log::Category::Render, Source);
    }
    return {};
}

void Dx12Backend::ShutdownFrame(FrameContext& frame)
{
    for (uint32_t index = 0; index < StreamArenaCount; ++index) {
        if (frame.streams[index] && frame.streamsMapped[index])
            frame.streams[index]->Unmap(0, nullptr);
    }
    if (frame.constants && frame.constantsMapped)
        frame.constants->Unmap(0, nullptr);
    frame = {};
}

void Dx12Backend::ExecuteResourceCommands(ResourceCommandList& commands)
{
    ELM_ASSERT_THREAD(Render);
    if (commands.Empty())
        return;
    EnsureRecording();
    if (!m_recording)
        return;
    ResourceExecutor executor(*this);
    commands.Execute(executor);
    commands.Clear();
}

GpuTexture* Dx12Backend::FindTexture(TextureHandle handle)
{
    if (handle.Index() & TransientIndexBit)
        return m_transientTextures.Find(TextureHandle(handle.Index() & ~TransientIndexBit, handle.Generation()));
    return m_textures.Find(handle);
}

const GpuTexture* Dx12Backend::FindTexture(TextureHandle handle) const
{
    return const_cast<Dx12Backend*>(this)->FindTexture(handle);
}

GpuSurface* Dx12Backend::FindSurface(SurfaceId surface)
{
    if (surface == MainSurface)
        return m_mainSurface.swapChain ? &m_mainSurface : nullptr;
    const auto found = m_surfaces.find(surface);
    return found != m_surfaces.end() ? &found->second : nullptr;
}

const GpuSurface* Dx12Backend::FindSurface(SurfaceId surface) const
{
    return const_cast<Dx12Backend*>(this)->FindSurface(surface);
}

bool Dx12Backend::GetTextureDesc(TextureHandle texture, TextureDesc& desc) const
{
    ELM_ASSERT_THREAD(Render);
    const auto* gpu = FindTexture(texture);
    if (!gpu)
        return false;
    desc = gpu->desc;
    return true;
}

GpuTexture Dx12Backend::CreateTexture(const TextureDesc& desc)
{
    GpuTexture texture;
    texture.desc = desc;
    const auto releaseDescriptors = [&] {
        m_srvDescriptors.Free(texture.srvIndex);
        m_rtvDescriptors.Free(texture.rtvIndex);
        m_dsvDescriptors.Free(texture.dsvIndex);
        texture.srvIndex = InvalidDescriptor;
        texture.rtvIndex = InvalidDescriptor;
        texture.dsvIndex = InvalidDescriptor;
    };
    const DXGI_FORMAT format = ToDxgiFormat(desc.format);
    if (desc.width == 0 || desc.height == 0 || format == DXGI_FORMAT_UNKNOWN || !m_device) {
        ERROR_MESSAGE(log::Category::Render, Source, "Texture '%s' has no size or supported format", desc.name.c_str());
        return texture;
    }

    D3D12_RESOURCE_DESC resourceDesc {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (desc.bindFlags & TextureBind::RenderTarget)
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc.bindFlags & TextureBind::DepthStencil)
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT result = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.resource));
    if (FAILED(result)) {
        LogFailure("Create texture resource", result);
        return {};
    }
    texture.state = D3D12_RESOURCE_STATE_COMMON;
    texture.memoryBytes = TextureMemorySize(desc);
    if (desc.bindFlags & TextureBind::ShaderResource) {
        texture.srvIndex = m_srvDescriptors.Allocate();
        if (texture.srvIndex == InvalidDescriptor) {
            ERROR_MESSAGE(log::Category::Render, Source, "Shader-resource descriptor heap is full");
            releaseDescriptors();
            return {};
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view {};
        view.Format = format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(texture.resource.Get(), &view, m_srvDescriptors.Cpu(texture.srvIndex));
    }
    if (desc.bindFlags & TextureBind::RenderTarget) {
        texture.rtvIndex = m_rtvDescriptors.Allocate();
        if (texture.rtvIndex == InvalidDescriptor) {
            ERROR_MESSAGE(log::Category::Render, Source, "Render-target descriptor heap is full");
            releaseDescriptors();
            return {};
        }
        D3D12_RENDER_TARGET_VIEW_DESC view {};
        view.Format = format;
        view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(texture.resource.Get(), &view, m_rtvDescriptors.Cpu(texture.rtvIndex));
    }
    if (desc.bindFlags & TextureBind::DepthStencil) {
        texture.dsvIndex = m_dsvDescriptors.Allocate();
        if (texture.dsvIndex == InvalidDescriptor) {
            ERROR_MESSAGE(log::Category::Render, Source, "Depth-stencil descriptor heap is full");
            releaseDescriptors();
            return {};
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC view {};
        view.Format = format;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        m_device->CreateDepthStencilView(texture.resource.Get(), &view, m_dsvDescriptors.Cpu(texture.dsvIndex));
    }
    return texture;
}

GpuBuffer Dx12Backend::CreateBuffer(const BufferDesc& desc, D3D12_RESOURCE_STATES state)
{
    GpuBuffer buffer;
    buffer.desc = desc;
    if (desc.size == 0 || !m_device) {
        ERROR_MESSAGE(log::Category::Render, Source, "Buffer '%s' has no size", desc.name.c_str());
        return buffer;
    }
    buffer.resource = CreateCommittedBuffer(desc.size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    if (!buffer.resource)
        return {};
    buffer.state = D3D12_RESOURCE_STATE_COMMON;
    buffer.memoryBytes = static_cast<size_t>(desc.size);
    if (state != D3D12_RESOURCE_STATE_COMMON)
        Transition(buffer.resource.Get(), buffer.state, state);
    return buffer;
}

ComPtr<ID3D12Resource> Dx12Backend::CreateCommittedBuffer(uint64_t size, D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) const
{
    ComPtr<ID3D12Resource> resource;
    if (!m_device || size == 0)
        return resource;
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    D3D12_HEAP_PROPERTIES heap = HeapProperties(heapType);
    const HRESULT result = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result))
        LogFailure("Create buffer resource", result);
    return resource;
}

TextureHandle Dx12Backend::CreateTransientTexture(const TextureDesc& desc)
{
    ELM_ASSERT_THREAD(Render);
    auto texture = CreateTexture(desc);
    if (!texture.resource)
        return {};
    m_allocatedMemory += texture.memoryBytes;
    const TextureHandle slot = m_transientHandles.Allocate();
    m_transientTextures.Insert(slot, std::move(texture));
    return TextureHandle(slot.Index() | TransientIndexBit, slot.Generation());
}

void Dx12Backend::DestroyTransientTexture(TextureHandle texture)
{
    ELM_ASSERT_THREAD(Render);
    if (!(texture.Index() & TransientIndexBit))
        return;
    const TextureHandle slot(texture.Index() & ~TransientIndexBit, texture.Generation());
    GpuTexture removed;
    if (!m_transientTextures.Remove(slot, removed))
        return;
    m_transientHandles.Free(slot);
    Retire(std::move(removed));
}

ShaderHandle Dx12Backend::CreateShader(const ShaderDesc& desc)
{
    const String fileName = DxilFileName(desc);
    const String bytes = loadShaderSource(fileName);
    if (bytes.empty()) {
        ERROR_MESSAGE(log::Category::Render, Source, "Shader '%s' is missing; build the DXIL shader target", fileName.c_str());
        return {};
    }
    GpuShader shader;
    shader.stage = desc.stage;
    shader.bytecode.resize(bytes.size());
    std::memcpy(shader.bytecode.data(), bytes.data(), bytes.size());
    const ShaderHandle handle = m_shaderHandles.Allocate();
    m_shaders.Insert(handle, std::move(shader));
    return handle;
}

PipelineHandle Dx12Backend::CreatePipeline(const PipelineDesc& desc)
{
    const auto* vertexShader = m_shaders.Find(desc.vertexShader);
    const auto* pixelShader = m_shaders.Find(desc.pixelShader);
    if (!vertexShader || !pixelShader || vertexShader->stage != ShaderStage::Vertex || pixelShader->stage != ShaderStage::Pixel) {
        ERROR_MESSAGE(log::Category::Render, Source, "Pipeline '%s' references missing or mismatched shaders", desc.name.c_str());
        return {};
    }

    uint32_t textureCount = 0;
    for (const auto& binding : desc.bindings)
        textureCount += binding.type == BindingType::Texture ? 1u : 0u;
    if (textureCount > 1) {
        ERROR_MESSAGE(log::Category::Render, Source, "Pipeline '%s' exceeds the supported texture binding count", desc.name.c_str());
        return {};
    }
    for (const auto& sampler : desc.samplers) {
        const auto texture = std::find_if(desc.bindings.begin(), desc.bindings.end(), [&](const BindingDesc& binding) {
            return binding.type == BindingType::Texture && binding.name == sampler.textureName &&
                binding.stage == sampler.stage;
        });
        if (texture == desc.bindings.end()) {
            ERROR_MESSAGE(log::Category::Render, Source, "Sampler '%s_sampler' has no matching texture binding",
                sampler.textureName.c_str());
            return {};
        }
    }

    GpuPipeline pipeline;
    pipeline.scissor = desc.scissor;
    pipeline.topology = desc.topology == PrimitiveTopology::LineList
        ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    for (const auto& attribute : desc.vertexLayout) {
        if (pipeline.vertexStrides.size() <= attribute.bufferSlot)
            pipeline.vertexStrides.resize(attribute.bufferSlot + 1);
        pipeline.vertexStrides[attribute.bufferSlot] += VertexFormatSize(attribute.format);
        pipeline.vertexBufferCount = (std::max)(pipeline.vertexBufferCount, attribute.bufferSlot + 1);
    }
    // Registers follow the order of bindings within each stage (b0, b1.., t0.., s0..); the HLSL declares them
    // explicitly with register(...), so they do not depend on how DXC assigns unbound resources
    Vector<D3D12_ROOT_PARAMETER> rootParameters(desc.bindings.size());
    Vector<D3D12_DESCRIPTOR_RANGE> ranges;
    Vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    uint32_t constantRegisters[2] { 0, 0 };
    uint32_t textureRegisters[2] { 0, 0 };
    uint32_t samplerRegisters[2] { 0, 0 };
    for (uint32_t slot = 0; slot < desc.bindings.size(); ++slot) {
        const auto& binding = desc.bindings[slot];
        const uint32_t stage = static_cast<uint32_t>(binding.stage);
        auto& root = rootParameters[slot];
        root.ShaderVisibility = ShaderVisibility(binding.stage);
        if (binding.type == BindingType::Constants) {
            root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            root.Descriptor.ShaderRegister = constantRegisters[stage]++;
            root.Descriptor.RegisterSpace = 0;
            pipeline.constants.push_back({ slot, slot, binding.size });
        } else {
            D3D12_DESCRIPTOR_RANGE range {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = textureRegisters[stage]++;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            ranges.push_back(range);
            root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root.DescriptorTable.NumDescriptorRanges = 1;
            root.DescriptorTable.pDescriptorRanges = &ranges.back();
            pipeline.textureBindingSlot = slot;
            pipeline.textureRootParameter = slot;
        }
    }

    for (uint32_t index = 0; index < desc.samplers.size(); ++index) {
        const uint32_t stage = static_cast<uint32_t>(desc.samplers[index].stage);
        D3D12_STATIC_SAMPLER_DESC sampler {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = samplerRegisters[stage]++;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = ShaderVisibility(desc.samplers[index].stage);
        samplers.push_back(sampler);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc {};
    rootDesc.NumParameters = static_cast<UINT>(rootParameters.size());
    rootDesc.pParameters = rootParameters.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(result)) {
        if (errors)
            ERROR_MESSAGE(log::Category::Render, Source, "Root signature '%s': %s", desc.name.c_str(),
                static_cast<const char*>(errors->GetBufferPointer()));
        else
            LogFailure("D3D12SerializeRootSignature", result);
        return {};
    }
    result = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&pipeline.rootSignature));
    if (FAILED(result)) {
        LogFailure("CreateRootSignature", result);
        return {};
    }

    Vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (const auto& attribute : desc.vertexLayout) {
        D3D12_INPUT_ELEMENT_DESC element {};
        element.SemanticName = "ATTRIB";
        element.SemanticIndex = attribute.location;
        element.Format = ToDxgiFormat(attribute.format);
        element.InputSlot = attribute.bufferSlot;
        element.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        element.InputSlotClass = attribute.perInstance
            ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
            : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        element.InstanceDataStepRate = attribute.perInstance ? 1 : 0;
        inputElements.push_back(element);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso {};
    pso.pRootSignature = pipeline.rootSignature.Get();
    pso.VS = { vertexShader->bytecode.data(), vertexShader->bytecode.size() };
    pso.PS = { pixelShader->bytecode.data(), pixelShader->bytecode.size() };
    pso.InputLayout = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    auto& blend = pso.BlendState.RenderTarget[0];
    blend.BlendEnable = desc.blend != BlendMode::Opaque;
    blend.LogicOpEnable = FALSE;
    blend.SrcBlend = desc.blend == BlendMode::AlphaBlend ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = desc.cull == CullMode::Back ? D3D12_CULL_MODE_BACK
        : desc.cull == CullMode::Front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    pso.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    pso.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.RasterizerState.MultisampleEnable = FALSE;
    pso.RasterizerState.AntialiasedLineEnable = FALSE;
    pso.RasterizerState.ForcedSampleCount = 0;
    pso.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.DepthStencilState.DepthEnable = desc.depthTest;
    pso.DepthStencilState.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.PrimitiveTopologyType = desc.topology == PrimitiveTopology::LineList
        ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = ToDxgiFormat(desc.colorFormat);
    pso.DSVFormat = ToDxgiFormat(desc.depthFormat);
    pso.SampleDesc.Count = 1;
    result = m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline.state));
    if (FAILED(result)) {
        LogFailure("CreateGraphicsPipelineState", result);
        return {};
    }
    const PipelineHandle handle = m_pipelineHandles.Allocate();
    m_pipelines.Insert(handle, std::move(pipeline));
    return handle;
}

void Dx12Backend::DestroyPipeline(PipelineHandle pipeline)
{
    GpuPipeline removed;
    if (!m_pipelines.Remove(pipeline, removed))
        return;
    m_pipelineHandles.Free(pipeline);
    Retire(std::move(removed));
}

void Dx12Backend::UploadBuffer(GpuBuffer& destination, uint64_t offset, std::span<const uint8_t> data)
{
    if (data.empty())
        return;
    auto staging = CreateCommittedBuffer(data.size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!staging)
        return;
    void* mapped = nullptr;
    const D3D12_RANGE readRange { 0, 0 };
    HRESULT result = staging->Map(0, &readRange, &mapped);
    if (FAILED(result)) {
        LogFailure("Map buffer upload", result);
        return;
    }
    std::memcpy(mapped, data.data(), data.size());
    staging->Unmap(0, nullptr);
    Transition(destination.resource.Get(), destination.state, D3D12_RESOURCE_STATE_COPY_DEST);
    m_commandList->CopyBufferRegion(destination.resource.Get(), offset, staging.Get(), 0, data.size());
    Transition(destination.resource.Get(), destination.state, BufferState(destination.desc.type));
    CurrentFrame().staging.push_back(std::move(staging));
}

void Dx12Backend::UploadTexture(GpuTexture& texture, const TextureData& data)
{
    const uint32_t width = texture.desc.width;
    const uint32_t height = texture.desc.height;
    const size_t rowBytes = static_cast<size_t>(width) * TexturePixelSize;
    const size_t stride = data.stride > 0 ? data.stride : rowBytes;
    if (stride < rowBytes || data.data.size() < stride * (height - 1) + rowBytes) {
        ERROR_MESSAGE(log::Category::Render, Source, "Texture '%s' upload has the wrong size", texture.desc.name.c_str());
        return;
    }

    D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 uploadSize = 0;
    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &uploadSize);
    auto staging = CreateCommittedBuffer(uploadSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!staging)
        return;
    void* mapped = nullptr;
    const D3D12_RANGE readRange { 0, 0 };
    HRESULT result = staging->Map(0, &readRange, &mapped);
    if (FAILED(result)) {
        LogFailure("Map texture upload", result);
        return;
    }
    auto* destination = static_cast<uint8_t*>(mapped) + footprint.Offset;
    const auto* source = data.data.data();
    for (UINT row = 0; row < rows; ++row)
        std::memcpy(destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch, source + static_cast<size_t>(row) * stride,
            static_cast<size_t>(rowSize));
    staging->Unmap(0, nullptr);

    Transition(texture.resource.Get(), texture.state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION sourceLocation {};
    sourceLocation.pResource = staging.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION destinationLocation {};
    destinationLocation.pResource = texture.resource.Get();
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    m_commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
    const D3D12_RESOURCE_STATES nextState = texture.desc.bindFlags & TextureBind::ShaderResource
        ? static_cast<D3D12_RESOURCE_STATES>(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        : D3D12_RESOURCE_STATE_COMMON;
    Transition(texture.resource.Get(), texture.state, nextState);
    CurrentFrame().staging.push_back(std::move(staging));
}

void Dx12Backend::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& current, D3D12_RESOURCE_STATES target)
{
    if (!resource || current == target || !m_commandList)
        return;
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = current;
    barrier.Transition.StateAfter = target;
    m_commandList->ResourceBarrier(1, &barrier);
    current = target;
}

// ─────────────────────────────────────────────────────────────────────────────
// Surfaces and frame submission
// ─────────────────────────────────────────────────────────────────────────────

bool Dx12Backend::CreateSurfaceResources(GpuSurface& surface, const NativeWindow& window, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || !window.IsValid() || window.system != WindowSystem::Win32)
        return false;
    HRESULT result = S_OK;
    if (surface.swapChain) {
        ReleaseSurfaceResources(surface);
        result = surface.swapChain->ResizeBuffers(BufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result)) {
            LogFailure("ResizeBuffers", result);
            return false;
        }
        surface.windowHandle = window.handle;
    } else {
        DXGI_SWAP_CHAIN_DESC1 desc {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = BufferCount;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> swapChain;
        result = m_factory->CreateSwapChainForHwnd(m_queue.Get(), static_cast<HWND>(window.handle), &desc, nullptr, nullptr, &swapChain);
        if (FAILED(result)) {
            LogFailure("CreateSwapChainForHwnd", result);
            return false;
        }
        surface.windowHandle = window.handle;
        result = swapChain.As(&surface.swapChain);
        if (FAILED(result)) {
            LogFailure("Query IDXGISwapChain3", result);
            return false;
        }
        result = m_factory->MakeWindowAssociation(static_cast<HWND>(window.handle), DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result))
            LogFailure("MakeWindowAssociation", result);
    }

    surface.buffers.resize(BufferCount);
    surface.rtvIndices.resize(BufferCount, InvalidDescriptor);
    surface.states.resize(BufferCount, D3D12_RESOURCE_STATE_PRESENT);
    for (uint32_t index = 0; index < BufferCount; ++index) {
        result = surface.swapChain->GetBuffer(index, IID_PPV_ARGS(&surface.buffers[index]));
        if (FAILED(result)) {
            LogFailure("GetBuffer", result);
            ReleaseSurfaceResources(surface);
            return false;
        }
        surface.rtvIndices[index] = m_rtvDescriptors.Allocate();
        if (surface.rtvIndices[index] == InvalidDescriptor) {
            ERROR_MESSAGE(log::Category::Render, Source, "Render-target descriptor heap is full");
            ReleaseSurfaceResources(surface);
            return false;
        }
        D3D12_RENDER_TARGET_VIEW_DESC view {};
        view.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->CreateRenderTargetView(surface.buffers[index].Get(), &view, m_rtvDescriptors.Cpu(surface.rtvIndices[index]));
    }
    surface.width = width;
    surface.height = height;
    surface.bufferIndex = surface.swapChain->GetCurrentBackBufferIndex();
    return true;
}

void Dx12Backend::ReleaseSurfaceResources(GpuSurface& surface)
{
    for (const uint32_t index : surface.rtvIndices)
        m_rtvDescriptors.Free(index);
    surface.rtvIndices.clear();
    surface.buffers.clear();
    surface.states.clear();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Backend::CurrentRenderTarget(const GpuSurface& surface) const
{
    if (surface.bufferIndex >= surface.rtvIndices.size())
        return {};
    return m_rtvDescriptors.Cpu(surface.rtvIndices[surface.bufferIndex]);
}

void Dx12Backend::CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height)
{
    ELM_ASSERT_THREAD(Render);
    if (surface == MainSurface || width == 0 || height == 0 || !window.IsValid() || window.system != WindowSystem::Win32)
        return;
    DestroySurface(surface);
    GpuSurface created;
    if (!CreateSurfaceResources(created, window, width, height))
        return;
    m_surfaces.insert_or_assign(surface, std::move(created));
}

void Dx12Backend::DestroySurface(SurfaceId surface)
{
    ELM_ASSERT_THREAD(Render);
    if (surface == MainSurface || !m_surfaces.contains(surface))
        return;
    if (m_recording)
        ExecuteCommands();
    WaitForIdle();
    m_surfaces.erase(surface);
}

void Dx12Backend::ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height)
{
    ELM_ASSERT_THREAD(Render);
    if (width == 0 || height == 0)
        return;
    auto* target = FindSurface(surface);
    if (!target || (target->width == width && target->height == height))
        return;
    if (m_recording)
        ExecuteCommands();
    WaitForIdle();
    const NativeWindow window { target->windowHandle, nullptr, WindowSystem::Win32 };
    if (!CreateSurfaceResources(*target, window, width, height))
        ERROR_MESSAGE(log::Category::Render, Source, "Failed to resize DXGI surface %u", surface);
}

bool Dx12Backend::HasSurface(SurfaceId surface) const
{
    ELM_ASSERT_THREAD(Render);
    return FindSurface(surface) != nullptr;
}

TextureFormat Dx12Backend::GetSurfaceColorFormat() const
{
    return m_mainSurface.swapChain ? TextureFormat::BGRA8_UNORM_SRGB : TextureFormat::Unknown;
}

TextureFormat Dx12Backend::GetSurfaceDepthFormat() const
{
    return TextureFormat::Unknown;
}

void Dx12Backend::EnsureRecording()
{
    if (m_recording || !m_device || !m_commandList)
        return;
    m_frameSlot = (m_frameSlot + 1) % FramesInFlight;
    FrameContext& frame = CurrentFrame();
    WaitForFence(frame.fenceValue);
    ReleaseFrame(frame);
    const HRESULT allocatorResult = frame.allocator->Reset();
    if (FAILED(allocatorResult)) {
        LogFailure("Reset command allocator", allocatorResult);
        return;
    }
    const HRESULT listResult = m_commandList->Reset(frame.allocator.Get(), nullptr);
    if (FAILED(listResult)) {
        LogFailure("Reset command list", listResult);
        return;
    }
    frame.constantsOffset = 0;
    m_recording = true;
}

void Dx12Backend::ExecuteCommands()
{
    if (!m_recording)
        return;
    const auto submitSurface = [&](GpuSurface& surface) {
        for (size_t index = 0; index < surface.buffers.size(); ++index)
            Transition(surface.buffers[index].Get(), surface.states[index], D3D12_RESOURCE_STATE_PRESENT);
    };
    submitSurface(m_mainSurface);
    for (auto& [surfaceId, surface] : m_surfaces)
        submitSurface(surface);

    m_recording = false;
    const HRESULT result = m_commandList->Close();
    if (FAILED(result)) {
        LogFailure("Close command list", result);
        return;
    }
    ID3D12CommandList* lists[] { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    const HRESULT signalResult = m_queue->Signal(m_fence.Get(), m_fenceValue + 1);
    if (FAILED(signalResult)) {
        // Nothing will reach the value: waiting for it later would hang
        LogFailure("Signal frame fence", signalResult);
        return;
    }
    CurrentFrame().fenceValue = ++m_fenceValue;
}

void Dx12Backend::WaitForFence(uint64_t value)
{
    if (!m_fence || !m_fenceEvent || value == 0 || m_fence->GetCompletedValue() >= value)
        return;
    const HRESULT result = m_fence->SetEventOnCompletion(value, m_fenceEvent);
    if (FAILED(result)) {
        LogFailure("SetEventOnCompletion", result);
        return;
    }
    const DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
        ERROR_MESSAGE(log::Category::Render, Source, "Waiting for the GPU fence failed (status %lu)", waitResult);
}

void Dx12Backend::WaitForIdle()
{
    WaitForFence(m_fenceValue);
    for (auto& frame : m_frames)
        ReleaseFrame(frame);
}

rhi::SubmitStats Dx12Backend::Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads)
{
    ELM_ASSERT_THREAD(Render);
    EnsureRecording();
    if (!m_recording)
        return {};
    FrameContext& frame = CurrentFrame();
    for (uint32_t index = 0; index < StreamArenaCount; ++index) {
        const auto arena = static_cast<rhi::UploadArena>(index);
        const size_t used = uploads.GetUsedSize(arena);
        if (used > 0)
            std::memcpy(frame.streamsMapped[index], uploads.GetData(arena), used);
    }

    rhi::SubmitStats stats;
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
    return stats;
}

void Dx12Backend::Present(SurfaceId surface)
{
    ELM_ASSERT_THREAD(Render);
    if (m_recording)
        ExecuteCommands();
    auto* target = FindSurface(surface);
    if (!target || !target->swapChain)
        return;
    const HRESULT result = target->swapChain->Present(1, 0);
    if (FAILED(result)) {
        LogFailure("Present", result);
        return;
    }
    target->bufferIndex = target->swapChain->GetCurrentBackBufferIndex();
}

size_t Dx12Backend::GetAllocatedMemory() const
{
    return m_allocatedMemory.load(std::memory_order_relaxed);
}

void Dx12Backend::Retire(GpuTexture texture)
{
    CurrentFrame().retiredTextures.push_back(std::move(texture));
}

void Dx12Backend::Retire(GpuBuffer buffer)
{
    CurrentFrame().retiredBuffers.push_back(std::move(buffer));
}

void Dx12Backend::Retire(GpuMesh mesh)
{
    CurrentFrame().retiredMeshes.push_back(std::move(mesh));
}

void Dx12Backend::Retire(GpuPipeline pipeline)
{
    CurrentFrame().retiredPipelines.push_back(std::move(pipeline));
}

void Dx12Backend::ReleaseFrame(FrameContext& frame)
{
    for (const auto& texture : frame.retiredTextures) {
        m_srvDescriptors.Free(texture.srvIndex);
        m_rtvDescriptors.Free(texture.rtvIndex);
        m_dsvDescriptors.Free(texture.dsvIndex);
        m_allocatedMemory -= texture.memoryBytes;
    }
    for (const auto& buffer : frame.retiredBuffers)
        m_allocatedMemory -= buffer.memoryBytes;
    for (const auto& mesh : frame.retiredMeshes)
        m_allocatedMemory -= mesh.memoryBytes;
    for (const uint32_t descriptor : frame.shaderDescriptors)
        m_shaderDescriptors.Free(descriptor);
    frame.retiredTextures.clear();
    frame.retiredBuffers.clear();
    frame.retiredMeshes.clear();
    frame.retiredPipelines.clear();
    frame.shaderDescriptors.clear();
    frame.staging.clear();
}

} // namespace elm::render::dx12

namespace elm::render::rhi {

UniquePtr<IRenderBackend> createRenderBackend()
{
    return MakeUnique<dx12::Dx12Backend>();
}

} // namespace elm::render::rhi
