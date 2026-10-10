#include "render/backend/diligent/DiligentBackend.hpp"

#include "core/Debug.hpp"
#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Unexpected.hpp"

#include "render/ShaderSource.hpp"
#include "render/backend/diligent/Utils.hpp"

#include "Graphics/GraphicsEngine/interface/SwapChain.h"

#if PLATFORM_WIN32
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#else
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>

namespace elm::render::diligent {

namespace {

    class DiligentAllocator : public Diligent::IMemoryAllocator {
    public:
        virtual ~DiligentAllocator() = default;

        struct AllocationHeader {
            void* rawPointer;
            size_t requestedSize;
            size_t allocatedSize;
        };

        virtual void* Allocate(size_t Size, [[maybe_unused]] const char* DebugDesc, [[maybe_unused]] const char* File, [[maybe_unused]] int Line) override
        {
            constexpr size_t Alignment = 64;
            size_t allocatedSize = Size + Alignment + sizeof(AllocationHeader);
            void* rawPointer = memory::allocate_impl(allocatedSize);
            void* ptr = static_cast<char*>(rawPointer) + sizeof(AllocationHeader);
            size_t availableSize = allocatedSize - sizeof(AllocationHeader);
            std::align(Alignment, Size, ptr, availableSize);
            if (!ptr)
                return nullptr;

            auto* header = reinterpret_cast<AllocationHeader*>(ptr) - 1;
            header->rawPointer = rawPointer;
            header->requestedSize = Size;
            header->allocatedSize = allocatedSize;
            m_TotalAllocated.fetch_add(Size, std::memory_order_relaxed);

            return ptr;
        }

        virtual void Free(void* Ptr) override
        {
            if (!Ptr)
                return;

            auto* header = reinterpret_cast<AllocationHeader*>(Ptr) - 1;
            void* rawPointer = header->rawPointer;
            const size_t requestedSize = header->requestedSize;
            const size_t allocatedSize = header->allocatedSize;

            m_TotalAllocated.fetch_sub(requestedSize, std::memory_order_relaxed);
            memory::deallocate_impl(rawPointer, allocatedSize);
        }

        size_t GetTotalAllocatedBytes() const
        {
            return m_TotalAllocated.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<size_t> m_TotalAllocated { 0 };
    } g_Allocator;

    Diligent::SHADER_TYPE getShaderType(ShaderStage stage)
    {
        return stage == ShaderStage::Pixel ? Diligent::SHADER_TYPE_PIXEL : Diligent::SHADER_TYPE_VERTEX;
    }

    Diligent::RefCntAutoPtr<Diligent::IBuffer> createBuffer(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
        const char* name, BufferType type, ResourceUsage usage, uint64_t size, const void* data, uint64_t dataSize)
    {
        Diligent::BufferDesc desc;
        desc.Name = name;
        desc.BindFlags = getBindFlags(type);
        desc.Size = size;
        desc.Usage = usage == ResourceUsage::Dynamic ? Diligent::USAGE_DYNAMIC : Diligent::USAGE_DEFAULT;
        if (desc.Usage == Diligent::USAGE_DYNAMIC)
            desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;

        Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
        // Created empty and filled through the context: initial data would put a D3D12 buffer in a
        // state the immediate context does not know about
        device->CreateBuffer(desc, nullptr, &buffer);
        if (buffer && data && dataSize > 0 && desc.Usage == Diligent::USAGE_DEFAULT) {
            context->UpdateBuffer(buffer, 0, dataSize, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        return buffer;
    }

    Diligent::RESOURCE_STATE getResourceState(rhi::ResourceState state)
    {
        switch (state) {
        case rhi::ResourceState::RenderTarget:
            return Diligent::RESOURCE_STATE_RENDER_TARGET;
        case rhi::ResourceState::DepthWrite:
            return Diligent::RESOURCE_STATE_DEPTH_WRITE;
        case rhi::ResourceState::ShaderResource:
        default:
            return Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        }
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Resource commands recorded by RenderResources
// ─────────────────────────────────────────────────────────────────────────────

class DiligentBackend::ResourceExecutor {
public:
    explicit ResourceExecutor(DiligentBackend& backend) noexcept
        : m_backend(backend)
    {
    }

    void Execute(command::resource::CreateTexture& command)
    {
        auto texture = m_backend.CreateNativeTexture(command.desc);
        if (texture)
            m_backend.m_textures.Insert(command.handle, { std::move(texture), std::move(command.desc) });
    }

    void Execute(command::resource::ResizeTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (!gpu || (gpu->desc.width == command.width && gpu->desc.height == command.height))
            return;
        auto desc = gpu->desc;
        desc.width = command.width;
        desc.height = command.height;
        // The old texture is released by Diligent once the GPU no longer uses it
        if (auto texture = m_backend.CreateNativeTexture(desc)) {
            gpu->texture = std::move(texture);
            gpu->desc = std::move(desc);
        }
    }

    void Execute(command::resource::UploadTexture& command)
    {
        auto* gpu = m_backend.m_textures.Find(command.handle);
        if (!gpu || !gpu->texture)
            return;

        Diligent::Box updateBox;
        updateBox.MaxX = gpu->desc.width;
        updateBox.MaxY = gpu->desc.height;

        Diligent::TextureSubResData subresData;
        subresData.Stride = command.data.stride > 0 ? command.data.stride : static_cast<Diligent::Uint64>(gpu->desc.width) * 4;
        subresData.pData = command.data.data.data();

        m_backend.m_context->UpdateTexture(gpu->texture, 0, 0, updateBox, subresData,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void Execute(command::resource::DestroyTexture& command)
    {
        GpuTexture removed;
        m_backend.m_textures.Remove(command.handle, removed);
        // Pipelines cache bindings per texture
        m_backend.m_pipelines.ForEach([&](GpuPipeline& pipeline) { pipeline.bindings.erase(command.handle); });
    }

    void Execute(command::resource::CreateBuffer& command)
    {
        auto buffer = createBuffer(m_backend.m_device, m_backend.m_context, command.desc.name.c_str(), command.desc.type,
            command.desc.usage, command.desc.size, command.initialData.data(), command.initialData.size());
        if (buffer)
            m_backend.m_buffers.Insert(command.handle, { std::move(buffer), std::move(command.desc) });
    }

    void Execute(command::resource::UploadBuffer& command)
    {
        auto* gpu = m_backend.m_buffers.Find(command.handle);
        if (!gpu || !gpu->buffer || command.offset + command.data.size() > gpu->desc.size)
            return;
        m_backend.m_context->UpdateBuffer(gpu->buffer, command.offset, command.data.size(), command.data.data(),
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void Execute(command::resource::DestroyBuffer& command)
    {
        GpuBuffer removed;
        m_backend.m_buffers.Remove(command.handle, removed);
    }

    void Execute(command::resource::CreateMesh& command)
    {
        if (!command.data || command.data->vertices.empty() || command.data->indices.empty())
            return;
        const auto& data = *command.data;
        const uint64_t vertexBytes = data.vertices.size() * sizeof(Vertex);
        const uint64_t indexBytes = data.indices.size() * sizeof(uint32_t);

        GpuMesh mesh;
        mesh.vertexBuffer = createBuffer(m_backend.m_device, m_backend.m_context, "Mesh VB", BufferType::Vertex,
            ResourceUsage::Default, vertexBytes, data.vertices.data(), vertexBytes);
        mesh.indexBuffer = createBuffer(m_backend.m_device, m_backend.m_context, "Mesh IB", BufferType::Index,
            ResourceUsage::Default, indexBytes, data.indices.data(), indexBytes);
        mesh.indexCount = static_cast<uint32_t>(data.indices.size());
        if (mesh.vertexBuffer && mesh.indexBuffer)
            m_backend.m_meshes.Insert(command.handle, std::move(mesh));
    }

    void Execute(command::resource::DestroyMesh& command)
    {
        GpuMesh removed;
        m_backend.m_meshes.Remove(command.handle, removed);
    }

private:
    DiligentBackend& m_backend;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command list translation
// ─────────────────────────────────────────────────────────────────────────────

class DiligentBackend::CommandTranslator {
public:
    CommandTranslator(DiligentBackend& backend, const rhi::UploadAllocator& uploads) noexcept
        : m_backend(backend)
        , m_context(backend.m_context)
        , m_uploads(uploads)
    {
    }

    void Execute(const rhi::cmd::Barrier& command)
    {
        auto* gpu = m_backend.FindTexture(command.texture);
        if (!gpu || !gpu->texture)
            return;
        const auto state = getResourceState(command.state);
        if (gpu->texture->GetState() == state)
            return;
        Diligent::StateTransitionDesc barrier { gpu->texture, Diligent::RESOURCE_STATE_UNKNOWN, state,
            Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE };
        m_context->TransitionResourceStates(1, &barrier);
    }

    void Execute(const rhi::cmd::BeginRenderPass& command)
    {
        Diligent::ITextureView* rtv = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        if (command.color.useSurface) {
            if (auto* surface = m_backend.FindSurface(command.color.surface)) {
                rtv = surface->GetCurrentBackBufferRTV();
                width = surface->GetDesc().Width;
                height = surface->GetDesc().Height;
            }
        } else if (auto* gpu = m_backend.FindTexture(command.color.texture); gpu && gpu->texture) {
            rtv = gpu->texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            width = gpu->desc.width;
            height = gpu->desc.height;
        }

        Diligent::ITextureView* dsv = nullptr;
        if (auto* gpu = m_backend.FindTexture(command.depth.texture); gpu && gpu->texture)
            dsv = gpu->texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);

        m_passActive = rtv != nullptr;
        if (!m_passActive)
            return;

        m_context->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (command.color.clear)
            m_context->ClearRenderTarget(rtv, command.color.clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        if (dsv && command.depth.clear)
            m_context->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, command.depth.clearDepth, 0,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

        m_targetWidth = width;
        m_targetHeight = height;
        const Diligent::Viewport viewport { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        m_context->SetViewports(1, &viewport, width, height);
    }

    void Execute(const rhi::cmd::EndRenderPass&)
    {
        m_passActive = false;
        m_pipeline = nullptr;
    }

    void Execute(const rhi::cmd::SetPipeline& command)
    {
        m_pipeline = m_backend.m_pipelines.Find(command.pipeline);
        if (m_pipeline && m_pipeline->pso)
            m_context->SetPipelineState(m_pipeline->pso);
        m_boundSrb = nullptr;
        m_texture = {};
        m_fallbackTexture = {};
    }

    void Execute(const rhi::cmd::SetViewport& command)
    {
        const Diligent::Viewport viewport { command.x, command.y, command.width, command.height, 0.0f, 1.0f };
        m_context->SetViewports(1, &viewport, m_targetWidth, m_targetHeight);
    }

    void Execute(const rhi::cmd::SetScissor& command)
    {
        const Diligent::Rect rect { command.left, command.top, command.right, command.bottom };
        m_context->SetScissorRects(1, &rect, m_targetWidth, m_targetHeight);
    }

    void Execute(const rhi::cmd::BindVertexBuffer& command)
    {
        if (command.slot >= MaxVertexSlots)
            return;
        m_vertexBuffers[command.slot] = Resolve(command.source, m_vertexOffsets[command.slot]);
        m_vertexSlotCount = (std::max)(m_vertexSlotCount, command.slot + 1);
        m_vertexBuffersDirty = true;
    }

    void Execute(const rhi::cmd::BindIndexBuffer& command)
    {
        m_indexBuffer = Resolve(command.source, m_indexOffset);
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
            if (constants.slot != command.slot || !constants.buffer)
                continue;
            void* mapped = nullptr;
            m_context->MapBuffer(constants.buffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped);
            if (mapped) {
                const auto size = (std::min)(static_cast<uint64_t>(command.data.size), constants.buffer->GetDesc().Size);
                std::memcpy(mapped, m_uploads.GetData(rhi::UploadArena::Constants) + command.data.offset, size);
                m_context->UnmapBuffer(constants.buffer, Diligent::MAP_WRITE);
            }
        }
    }

    void Execute(const rhi::cmd::DrawIndexed& command)
    {
        if (!m_passActive || !m_pipeline || !m_pipeline->pso || !m_indexBuffer)
            return;

        auto* srb = SelectBinding();
        if (!srb)
            return;
        if (srb != m_boundSrb) {
            m_context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_boundSrb = srb;
        }
        if (m_vertexBuffersDirty) {
            m_context->SetVertexBuffers(0, m_vertexSlotCount, m_vertexBuffers, m_vertexOffsets,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
            m_vertexBuffersDirty = false;
        }
        if (m_indexBufferDirty) {
            m_context->SetIndexBuffer(m_indexBuffer, m_indexOffset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_indexBufferDirty = false;
        }

        const uint32_t indexCount = command.indexCount != 0 ? command.indexCount : m_meshIndexCount;
        if (indexCount == 0 || command.instanceCount == 0)
            return;
        Diligent::DrawIndexedAttribs draw { indexCount, Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_ALL };
        draw.NumInstances = command.instanceCount;
        draw.FirstIndexLocation = command.firstIndex;
        draw.BaseVertex = command.baseVertex;
        m_context->DrawIndexed(draw);
        ++m_drawCount;
    }

    [[nodiscard]] uint32_t GetDrawCount() const noexcept { return m_drawCount; }

private:
    static constexpr uint32_t MaxVertexSlots = 4;

    Diligent::IBuffer* Resolve(const rhi::BufferSource& source, Diligent::Uint64& offset)
    {
        offset = 0;
        switch (source.kind) {
        case rhi::BufferSource::Kind::MeshVertices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return mesh->vertexBuffer;
            return nullptr;
        case rhi::BufferSource::Kind::MeshIndices:
            if (const auto* mesh = m_backend.m_meshes.Find(source.mesh))
                return mesh->indexBuffer;
            return nullptr;
        case rhi::BufferSource::Kind::Buffer:
            offset = source.offset;
            if (const auto* buffer = m_backend.m_buffers.Find(source.buffer))
                return buffer->buffer;
            return nullptr;
        case rhi::BufferSource::Kind::Upload: {
            const auto arena = static_cast<size_t>(source.upload.arena);
            if (!source.upload.IsValid() || arena >= 2)
                return nullptr;
            offset = source.upload.offset;
            return m_backend.m_uploadBuffers[arena];
        }
        case rhi::BufferSource::Kind::None:
        default:
            return nullptr;
        }
    }

    Diligent::IShaderResourceBinding* SelectBinding()
    {
        if (m_pipeline->textures.empty())
            return m_pipeline->defaultBinding;

        TextureHandle handle = m_texture;
        auto* gpu = m_backend.FindTexture(handle);
        if (!gpu || !gpu->texture) {
            handle = m_fallbackTexture;
            gpu = m_backend.FindTexture(handle);
        }
        if (!gpu || !gpu->texture)
            return nullptr;

        auto& binding = m_pipeline->bindings[handle];
        if (!binding.srb || binding.texture != gpu->texture.RawPtr()) {
            binding = {};
            m_pipeline->pso->CreateShaderResourceBinding(&binding.srb, true);
            if (!binding.srb)
                return nullptr;
            const auto& slot = m_pipeline->textures.front();
            if (auto* variable = binding.srb->GetVariableByName(slot.stage, slot.name.c_str()))
                variable->Set(gpu->texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
            binding.texture = gpu->texture.RawPtr();
        }
        return binding.srb;
    }

    DiligentBackend& m_backend;
    Diligent::IDeviceContext* m_context;
    const rhi::UploadAllocator& m_uploads;

    bool m_passActive { false };
    uint32_t m_drawCount { 0 };
    uint32_t m_targetWidth { 0 };
    uint32_t m_targetHeight { 0 };

    GpuPipeline* m_pipeline { nullptr };
    Diligent::IShaderResourceBinding* m_boundSrb { nullptr };
    TextureHandle m_texture;
    TextureHandle m_fallbackTexture;

    Diligent::IBuffer* m_vertexBuffers[MaxVertexSlots] {};
    Diligent::Uint64 m_vertexOffsets[MaxVertexSlots] {};
    uint32_t m_vertexSlotCount { 0 };
    bool m_vertexBuffersDirty { false };

    Diligent::IBuffer* m_indexBuffer { nullptr };
    Diligent::Uint64 m_indexOffset { 0 };
    uint32_t m_meshIndexCount { 0 };
    bool m_indexBufferDirty { false };
};

// ─────────────────────────────────────────────────────────────────────────────
// Backend
// ─────────────────────────────────────────────────────────────────────────────

DiligentBackend::DiligentBackend() = default;

DiligentBackend::~DiligentBackend()
{
    Shutdown();
}

auto DiligentBackend::Init(const NativeWindow& window, Size size) -> EngineResult<void>
{
#if PLATFORM_WIN32
    auto* pFactory = Diligent::GetEngineFactoryD3D12();
    if (!pFactory) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryD3D12",
            log::Category::Render, "DiligentBackend");
    }

    Diligent::EngineD3D12CreateInfo engineCreateInfo;
    engineCreateInfo.NumDeferredContexts = 0;
    engineCreateInfo.DynamicHeapPageSize = 8 << 20;
    engineCreateInfo.pRawMemAllocator = &g_Allocator;
    pFactory->CreateDeviceAndContextsD3D12(engineCreateInfo, &m_device, &m_context);
#else
    auto* pFactory = Diligent::GetEngineFactoryVk();
    if (!pFactory) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to load Diligent EngineFactoryVk",
            log::Category::Render, "DiligentBackend");
    }

    Diligent::EngineVkCreateInfo engineCreateInfo;
    engineCreateInfo.NumDeferredContexts = 0;
    engineCreateInfo.DynamicHeapSize = 128 << 20;
    engineCreateInfo.DynamicHeapPageSize = 8 << 20;
    engineCreateInfo.pRawMemAllocator = &g_Allocator;
    pFactory->CreateDeviceAndContextsVk(engineCreateInfo, &m_device, &m_context);
#endif

    if (!m_device || !m_context) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create Diligent Render Device & Contexts",
            log::Category::Render, "DiligentBackend");
    }

    m_mainSurface = CreateSwapChain(window, size.width, size.height, true);
    if (!m_mainSurface || !m_mainSurface.GetCurrentBackBufferRTV() || !m_mainSurface.GetDepthBufferDSV()) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed,
            "Failed to create Diligent SwapChain with required color and depth-stencil views",
            log::Category::Render, "DiligentBackend");
    }

    // The upload arenas are copied with UpdateBuffer, which only transfers the used bytes
    m_uploadBuffers[0] = createBuffer(m_device, m_context, "Upload Vertex Stream", BufferType::Vertex,
        ResourceUsage::Default, rhi::UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Vertex), nullptr, 0);
    m_uploadBuffers[1] = createBuffer(m_device, m_context, "Upload Index Stream", BufferType::Index,
        ResourceUsage::Default, rhi::UploadAllocator::GetDefaultCapacity(rhi::UploadArena::Index), nullptr, 0);
    if (!m_uploadBuffers[0] || !m_uploadBuffers[1]) {
        return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, "Failed to create upload buffers",
            log::Category::Render, "DiligentBackend");
    }
    return {};
}

void DiligentBackend::Shutdown()
{
    if (!m_device)
        return;

    if (m_context)
        m_context->Flush();

    m_pipelines.Clear();
    m_shaders.Clear();
    m_transientTextures.Clear();
    m_textures.Clear();
    m_buffers.Clear();
    m_meshes.Clear();
    for (auto& buffer : m_uploadBuffers)
        buffer.Release();
    m_surfaces.clear();
    m_mainSurface.Reset();
    m_context.Release();
    m_device.Release();
}

void DiligentBackend::ExecuteResourceCommands(ResourceCommandList& commands)
{
    ResourceExecutor executor(*this);
    commands.Execute(executor);
    commands.Clear();
}

GpuTexture* DiligentBackend::FindTexture(TextureHandle handle)
{
    if (handle.Index() & TransientIndexBit)
        return m_transientTextures.Find(TextureHandle(handle.Index() & ~TransientIndexBit, handle.Generation()));
    return m_textures.Find(handle);
}

const GpuTexture* DiligentBackend::FindTexture(TextureHandle handle) const
{
    return const_cast<DiligentBackend*>(this)->FindTexture(handle);
}

bool DiligentBackend::GetTextureDesc(TextureHandle texture, TextureDesc& desc) const
{
    const auto* gpu = FindTexture(texture);
    if (!gpu)
        return false;
    desc = gpu->desc;
    return true;
}

TextureHandle DiligentBackend::CreateTransientTexture(const TextureDesc& desc)
{
    auto texture = CreateNativeTexture(desc);
    if (!texture)
        return {};
    const auto slot = m_transientHandles.Allocate();
    m_transientTextures.Insert(slot, { std::move(texture), desc });
    return TextureHandle(slot.Index() | TransientIndexBit, slot.Generation());
}

void DiligentBackend::DestroyTransientTexture(TextureHandle texture)
{
    if (!(texture.Index() & TransientIndexBit))
        return;
    const TextureHandle slot(texture.Index() & ~TransientIndexBit, texture.Generation());
    GpuTexture removed;
    if (m_transientTextures.Remove(slot, removed))
        m_transientHandles.Free(slot);
    m_pipelines.ForEach([&](GpuPipeline& pipeline) { pipeline.bindings.erase(texture); });
}

ShaderHandle DiligentBackend::CreateShader(const ShaderDesc& desc)
{
    const String source = loadShaderSource(desc.sourceFile);
    if (source.empty()) {
        ERROR_MESSAGE(log::Category::Render, "DiligentBackend", "Shader source '%s' is missing", desc.sourceFile.c_str());
        return {};
    }

    Vector<Diligent::ShaderMacro> macros;
    for (const auto& macro : desc.macros)
        macros.push_back({ macro.name.c_str(), macro.value.c_str() });

    Diligent::ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shaderCI.Desc.ShaderType = getShaderType(desc.stage);
    shaderCI.Desc.Name = desc.name.c_str();
    shaderCI.Source = source.c_str();
    if (!macros.empty())
        shaderCI.Macros = { macros.data(), static_cast<Diligent::Uint32>(macros.size()) };

    GpuShader shader;
    m_device->CreateShader(shaderCI, &shader.shader);
    if (!shader.shader) {
        ERROR_MESSAGE(log::Category::Render, "DiligentBackend", "Failed to compile shader '%s'", desc.name.c_str());
        return {};
    }
    const auto handle = m_shaderHandles.Allocate();
    m_shaders.Insert(handle, std::move(shader));
    return handle;
}

PipelineHandle DiligentBackend::CreatePipeline(const PipelineDesc& desc)
{
    const auto* vs = m_shaders.Find(desc.vertexShader);
    const auto* ps = m_shaders.Find(desc.pixelShader);
    if (!vs || !ps)
        return {};

    Vector<Diligent::LayoutElement> layout;
    for (const auto& attribute : desc.vertexLayout) {
        const Diligent::Uint32 components = attribute.format == VertexFormat::Float2 ? 2 : attribute.format == VertexFormat::Float3 ? 3 : 4;
        layout.push_back(Diligent::LayoutElement { attribute.location, attribute.bufferSlot, components, Diligent::VT_FLOAT32, false,
            attribute.perInstance ? Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE : Diligent::INPUT_ELEMENT_FREQUENCY_PER_VERTEX });
    }

    Diligent::GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = desc.name.c_str();
    auto& pipeline = psoCI.GraphicsPipeline;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = getTextureFormat(desc.colorFormat);
    pipeline.DSVFormat = getTextureFormat(desc.depthFormat);
    pipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipeline.RasterizerDesc.CullMode = desc.cull == CullMode::Back ? Diligent::CULL_MODE_BACK
        : desc.cull == CullMode::Front                              ? Diligent::CULL_MODE_FRONT
                                                                    : Diligent::CULL_MODE_NONE;
    pipeline.RasterizerDesc.ScissorEnable = desc.scissor;
    pipeline.DepthStencilDesc.DepthEnable = desc.depthTest;
    pipeline.DepthStencilDesc.DepthWriteEnable = desc.depthWrite;
    pipeline.InputLayout.LayoutElements = layout.data();
    pipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(layout.size());

    auto& blend = pipeline.BlendDesc.RenderTargets[0];
    if (desc.blend != BlendMode::Opaque) {
        blend.BlendEnable = true;
        blend.SrcBlend = desc.blend == BlendMode::AlphaBlend ? Diligent::BLEND_FACTOR_SRC_ALPHA : Diligent::BLEND_FACTOR_ONE;
        blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    }
    psoCI.pVS = vs->shader;
    psoCI.pPS = ps->shader;

    // Constants are static variables bound once to a per-pipeline dynamic buffer; textures are mutable
    Vector<Diligent::ShaderResourceVariableDesc> variables;
    for (const auto& binding : desc.bindings) {
        if (binding.type == BindingType::Texture)
            variables.push_back({ getShaderType(binding.stage), binding.name.c_str(), Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE });
    }
    ELM_ASSERT(variables.size() <= 1);
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    psoCI.PSODesc.ResourceLayout.Variables = variables.data();
    psoCI.PSODesc.ResourceLayout.NumVariables = static_cast<Diligent::Uint32>(variables.size());

    Vector<String> samplerNames;
    Vector<Diligent::ImmutableSamplerDesc> samplers;
    samplerNames.reserve(desc.samplers.size());
    for (const auto& sampler : desc.samplers) {
        samplerNames.push_back(sampler.textureName + "_sampler");
        Diligent::ImmutableSamplerDesc samplerDesc;
        samplerDesc.ShaderStages = getShaderType(sampler.stage);
        samplerDesc.SamplerOrTextureName = samplerNames.back().c_str();
        samplerDesc.Desc.MinFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.Desc.MagFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.Desc.MipFilter = Diligent::FILTER_TYPE_LINEAR;
        samplerDesc.Desc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.Desc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplerDesc.Desc.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
        samplers.push_back(samplerDesc);
    }
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers.data();
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Diligent::Uint32>(samplers.size());

    GpuPipeline gpu;
    m_device->CreateGraphicsPipelineState(psoCI, &gpu.pso);
    if (!gpu.pso) {
        ERROR_MESSAGE(log::Category::Render, "DiligentBackend", "Failed to create pipeline '%s'", desc.name.c_str());
        return {};
    }

    for (uint32_t slot = 0; slot < desc.bindings.size(); ++slot) {
        const auto& binding = desc.bindings[slot];
        if (binding.type == BindingType::Texture) {
            gpu.textures.push_back({ slot, getShaderType(binding.stage), binding.name });
            continue;
        }
        GpuPipeline::ConstantSlot constants { slot, {} };
        Diligent::BufferDesc bufferDesc;
        bufferDesc.Name = binding.name.c_str();
        bufferDesc.Size = binding.size;
        bufferDesc.Usage = Diligent::USAGE_DYNAMIC;
        bufferDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        bufferDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        m_device->CreateBuffer(bufferDesc, nullptr, &constants.buffer);
        if (auto* variable = gpu.pso->GetStaticVariableByName(getShaderType(binding.stage), binding.name.c_str()))
            variable->Set(constants.buffer);
        gpu.constants.push_back(std::move(constants));
    }
    gpu.pso->CreateShaderResourceBinding(&gpu.defaultBinding, true);

    const auto handle = m_pipelineHandles.Allocate();
    m_pipelines.Insert(handle, std::move(gpu));
    return handle;
}

void DiligentBackend::DestroyPipeline(PipelineHandle pipeline)
{
    GpuPipeline removed;
    if (m_pipelines.Remove(pipeline, removed))
        m_pipelineHandles.Free(pipeline);
}

void DiligentBackend::UploadArena(rhi::UploadArena arena, const rhi::UploadAllocator& uploads)
{
    const auto index = static_cast<size_t>(arena);
    const size_t used = uploads.GetUsedSize(arena);
    if (used == 0 || !m_uploadBuffers[index])
        return;
    m_context->UpdateBuffer(m_uploadBuffers[index], 0, used, uploads.GetData(arena),
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

rhi::SubmitStats DiligentBackend::Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads)
{
    UploadArena(rhi::UploadArena::Vertex, uploads);
    UploadArena(rhi::UploadArena::Index, uploads);

    rhi::SubmitStats stats;
    for (const auto* list : lists) {
        if (!list)
            continue;
        // A fresh translator per list: lists are recorded independently and must not inherit state
        CommandTranslator translator(*this, uploads);
        for (const auto& command : list->GetCommands())
            std::visit([&translator](const auto& cmd) { translator.Execute(cmd); }, command);
        stats.drawCalls += translator.GetDrawCount();
        stats.commands += static_cast<uint32_t>(list->GetCommands().size());
    }
    return stats;
}

Diligent::RefCntAutoPtr<Diligent::ITexture> DiligentBackend::CreateNativeTexture(const TextureDesc& desc)
{
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    if (desc.width == 0 || desc.height == 0)
        return texture;

    Diligent::TextureDesc description;
    description.Name = desc.name.c_str();
    description.Type = Diligent::RESOURCE_DIM_TEX_2D;
    description.Width = desc.width;
    description.Height = desc.height;
    description.Format = getTextureFormat(desc.format);
    description.Usage = getUsage(desc.usage);
    description.BindFlags = getTextureBindFlags(desc.bindFlags);
    m_device->CreateTexture(description, nullptr, &texture);
    if (!texture)
        ERROR_MESSAGE(log::Category::Render, "DiligentBackend", "Failed to create texture '%s'", desc.name.c_str());
    return texture;
}

SwapChain DiligentBackend::CreateSwapChain(const NativeWindow& window, uint32_t width, uint32_t height, bool withDepth)
{
    if (!m_device || !window.IsValid())
        return {};

    Diligent::SwapChainDesc description = m_mainSurface ? m_mainSurface.GetDesc() : Diligent::SwapChainDesc {};
    description.Width = width;
    description.Height = height;
    if (!withDepth)
        description.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;

    Diligent::ISwapChain* swapChain = nullptr;
#if PLATFORM_WIN32
    auto* factory = Diligent::GetEngineFactoryD3D12();
    Diligent::Win32NativeWindow nativeWindow { window.handle };
    factory->CreateSwapChainD3D12(m_device, m_context, description,
        Diligent::FullScreenModeDesc {}, nativeWindow, &swapChain);
#else
    auto* factory = Diligent::GetEngineFactoryVk();
    Diligent::LinuxNativeWindow nativeWindow;
    nativeWindow.pDisplay = window.display;
    nativeWindow.WindowId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window.handle));
    factory->CreateSwapChainVk(m_device, m_context, description, nativeWindow, &swapChain);
#endif
    return SwapChain(swapChain);
}

SwapChain* DiligentBackend::FindSurface(SurfaceId surface)
{
    if (surface == MainSurface)
        return m_mainSurface ? &m_mainSurface : nullptr;
    const auto it = m_surfaces.find(surface);
    return it != m_surfaces.end() ? &it->second : nullptr;
}

bool DiligentBackend::HasSurface(SurfaceId surface) const
{
    return surface == MainSurface ? static_cast<bool>(m_mainSurface) : m_surfaces.contains(surface);
}

void DiligentBackend::ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (auto* swapChain = FindSurface(surface))
        swapChain->ResizeIfNeeded(width, height);
}

void DiligentBackend::CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height)
{
    if (surface == MainSurface || width == 0 || height == 0)
        return;
    if (auto swapChain = CreateSwapChain(window, width, height, false))
        m_surfaces.insert_or_assign(surface, std::move(swapChain));
}

void DiligentBackend::DestroySurface(SurfaceId surface)
{
    if (surface != MainSurface)
        m_surfaces.erase(surface);
}

void DiligentBackend::Present(SurfaceId surface)
{
    if (auto* swapChain = FindSurface(surface))
        swapChain->Present();
}

TextureFormat DiligentBackend::GetSurfaceColorFormat() const
{
    return m_mainSurface ? getEngineTextureFormat(m_mainSurface.GetDesc().ColorBufferFormat) : TextureFormat::Unknown;
}

TextureFormat DiligentBackend::GetSurfaceDepthFormat() const
{
    return m_mainSurface ? getEngineTextureFormat(m_mainSurface.GetDesc().DepthBufferFormat) : TextureFormat::Unknown;
}

size_t DiligentBackend::GetAllocatedMemory() const
{
    return g_Allocator.GetTotalAllocatedBytes();
}

} // namespace elm::render::diligent

namespace elm::render::rhi {

UniquePtr<IRenderBackend> createRenderBackend()
{
    return MakeUnique<diligent::DiligentBackend>();
}

} // namespace elm::render::rhi
