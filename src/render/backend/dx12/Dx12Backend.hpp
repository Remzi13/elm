#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/rhi/IRenderBackend.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <atomic>

namespace elm::render::dx12 {

    using Microsoft::WRL::ComPtr;

    struct GpuTexture {
        ComPtr<ID3D12Resource> resource;
        TextureDesc desc;
        D3D12_RESOURCE_STATES state { D3D12_RESOURCE_STATE_COMMON };
        uint32_t srvIndex { UINT32_MAX };
        uint32_t rtvIndex { UINT32_MAX };
        uint32_t dsvIndex { UINT32_MAX };
        size_t memoryBytes { 0 };
    };

    struct GpuBuffer {
        ComPtr<ID3D12Resource> resource;
        BufferDesc desc;
        D3D12_RESOURCE_STATES state { D3D12_RESOURCE_STATE_COMMON };
        size_t memoryBytes { 0 };
    };

    struct GpuMesh {
        GpuBuffer vertices;
        GpuBuffer indices;
        uint32_t indexCount { 0 };
        size_t memoryBytes { 0 };
    };

    struct GpuShader {
        ShaderStage stage { ShaderStage::Vertex };
        Vector<uint8_t> bytecode;
    };

    struct GpuPipeline {
        struct ConstantBinding {
            uint32_t slot { 0 };
            uint32_t rootParameter { 0 };
            uint32_t size { 0 };
        };

        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> state;
        Vector<ConstantBinding> constants;
        uint32_t textureBindingSlot { UINT32_MAX };
        uint32_t textureRootParameter { UINT32_MAX };
        Vector<uint32_t> vertexStrides;
        uint32_t vertexBufferCount { 0 };
        D3D12_PRIMITIVE_TOPOLOGY topology { D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
        bool scissor { false };
    };

    struct GpuSurface {
        ComPtr<IDXGISwapChain3> swapChain;
        Vector<ComPtr<ID3D12Resource>> buffers;
        Vector<uint32_t> rtvIndices;
        Vector<D3D12_RESOURCE_STATES> states;
        uint32_t bufferIndex { 0 };
        uint32_t width { 0 };
        uint32_t height { 0 };
        void* windowHandle { nullptr };
    };

    /// Native Direct3D 12 implementation. Render-thread only; stale handles are ignored.
    /// Init and Shutdown run while the render thread is stopped.
    ///
    /// Up to FramesInFlight frames are on the GPU. Each frame records into the shared command list with its own
    /// allocator and upload memory; resources destroyed during a frame are freed once the GPU finished it.
    class Dx12Backend final : public rhi::IRenderBackend {
    public:
        Dx12Backend() = default;
        ~Dx12Backend() override;

        Dx12Backend(const Dx12Backend&) = delete;
        Dx12Backend& operator=(const Dx12Backend&) = delete;
        Dx12Backend(Dx12Backend&&) = delete;
        Dx12Backend& operator=(Dx12Backend&&) = delete;

        [[nodiscard]] StringView GetName() const override { return "Direct3D 12"; }
        [[nodiscard]] auto Init(const NativeWindow& window, Size size) -> EngineResult<void> override;
        void Shutdown() override;

        void ExecuteResourceCommands(ResourceCommandList& commands) override;
        [[nodiscard]] bool GetTextureDesc(TextureHandle texture, TextureDesc& desc) const override;

        [[nodiscard]] TextureHandle CreateTransientTexture(const TextureDesc& desc) override;
        void DestroyTransientTexture(TextureHandle texture) override;
        [[nodiscard]] ShaderHandle CreateShader(const ShaderDesc& desc) override;
        [[nodiscard]] PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
        void DestroyPipeline(PipelineHandle pipeline) override;

        void CreateSurface(SurfaceId surface, const NativeWindow& window, uint32_t width, uint32_t height) override;
        void DestroySurface(SurfaceId surface) override;
        void ResizeSurface(SurfaceId surface, uint32_t width, uint32_t height) override;
        [[nodiscard]] bool HasSurface(SurfaceId surface) const override;
        [[nodiscard]] TextureFormat GetSurfaceColorFormat() const override;
        [[nodiscard]] TextureFormat GetSurfaceDepthFormat() const override;
        void Present(SurfaceId surface) override;

        rhi::SubmitStats Submit(std::span<const rhi::CommandList* const> lists, const rhi::UploadAllocator& uploads) override;
        [[nodiscard]] size_t GetAllocatedMemory() const override;

    private:
        class ResourceExecutor;
        class CommandTranslator;

        static constexpr uint32_t TransientIndexBit = 1u << (TextureHandle::IndexBits - 1);
        static constexpr uint32_t BufferCount = 2;
        /// Frames the CPU may record ahead of the GPU.
        static constexpr uint32_t FramesInFlight = 2;
        /// Vertex and Index upload arenas copied to the GPU each frame; Constants are copied per block.
        static constexpr uint32_t StreamArenaCount = 2;
        static constexpr uint32_t MaxVertexSlots = 4;
        static constexpr uint32_t RenderTargetDescriptorCapacity = 4096;
        static constexpr uint32_t DepthDescriptorCapacity = 1024;
        static constexpr uint32_t ShaderResourceDescriptorCapacity = 8192;
        static constexpr uint32_t ShaderDescriptorCapacity = 16384;
        static constexpr uint32_t InvalidDescriptor = UINT32_MAX;

        class DescriptorPool {
        public:
            [[nodiscard]] bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);
            [[nodiscard]] uint32_t Allocate();
            void Free(uint32_t index);
            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Cpu(uint32_t index) const;
            [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE Gpu(uint32_t index) const;
            [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const noexcept { return m_heap.Get(); }
            void Reset();

        private:
            ComPtr<ID3D12DescriptorHeap> m_heap;
            Vector<uint32_t> m_free;
            uint32_t m_capacity { 0 };
            uint32_t m_next { 0 };
            uint32_t m_increment { 0 };
        };

        /// What one frame in flight owns until the GPU has finished it (its fence value is reached).
        struct FrameContext {
            ComPtr<ID3D12CommandAllocator> allocator;
            std::array<ComPtr<ID3D12Resource>, StreamArenaCount> streams;
            std::array<uint8_t*, StreamArenaCount> streamsMapped {};
            ComPtr<ID3D12Resource> constants;
            uint8_t* constantsMapped { nullptr };
            uint64_t constantsOffset { 0 };
            /// Signalled when the GPU finished the frame's last submission; 0 before the first one.
            uint64_t fenceValue { 0 };
            Vector<uint32_t> shaderDescriptors;
            Vector<ComPtr<ID3D12Resource>> staging;
            Vector<GpuTexture> retiredTextures;
            Vector<GpuBuffer> retiredBuffers;
            Vector<GpuMesh> retiredMeshes;
            Vector<GpuPipeline> retiredPipelines;
        };

        [[nodiscard]] auto InitFrame(FrameContext& frame) -> EngineResult<void>;
        void ShutdownFrame(FrameContext& frame);
        /// The frame being recorded or, between frames, the last submitted one: it holds retired resources.
        [[nodiscard]] FrameContext& CurrentFrame() noexcept { return m_frames[m_frameSlot]; }
        /// Frees what the frame retired and its shader descriptors; the GPU must be done with it.
        void ReleaseFrame(FrameContext& frame);

        [[nodiscard]] GpuTexture* FindTexture(TextureHandle handle);
        [[nodiscard]] const GpuTexture* FindTexture(TextureHandle handle) const;
        [[nodiscard]] GpuSurface* FindSurface(SurfaceId surface);
        [[nodiscard]] const GpuSurface* FindSurface(SurfaceId surface) const;
        [[nodiscard]] GpuTexture CreateTexture(const TextureDesc& desc);
        [[nodiscard]] GpuBuffer CreateBuffer(const BufferDesc& desc, D3D12_RESOURCE_STATES state);
        [[nodiscard]] ComPtr<ID3D12Resource> CreateCommittedBuffer(uint64_t size, D3D12_HEAP_TYPE heapType,
            D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) const;
        [[nodiscard]] bool CreateSurfaceResources(GpuSurface& surface, const NativeWindow& window, uint32_t width, uint32_t height);
        void ReleaseSurfaceResources(GpuSurface& surface);
        void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES& current, D3D12_RESOURCE_STATES target);
        void UploadBuffer(GpuBuffer& destination, uint64_t offset, std::span<const uint8_t> data);
        void UploadTexture(GpuTexture& texture, const TextureData& data);
        /// Starts the next frame when none is recording: waits for its slot's previous submission.
        void EnsureRecording();
        void ExecuteCommands();
        void WaitForFence(uint64_t value);
        /// Waits for every submitted frame and releases all of them.
        void WaitForIdle();
        void Retire(GpuTexture texture);
        void Retire(GpuBuffer buffer);
        void Retire(GpuMesh mesh);
        void Retire(GpuPipeline pipeline);
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTarget(const GpuSurface& surface) const;

        ComPtr<IDXGIFactory6> m_factory;
        ComPtr<IDXGIAdapter1> m_adapter;
        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12CommandQueue> m_queue;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        std::array<FrameContext, FramesInFlight> m_frames;
        uint32_t m_frameSlot { 0 };
        ComPtr<ID3D12Fence> m_fence;
        HANDLE m_fenceEvent { nullptr };
        uint64_t m_fenceValue { 0 };
        bool m_recording { false };

        DescriptorPool m_rtvDescriptors;
        DescriptorPool m_dsvDescriptors;
        DescriptorPool m_srvDescriptors;
        DescriptorPool m_shaderDescriptors;

        GpuSurface m_mainSurface;
        UnorderedMap<SurfaceId, GpuSurface> m_surfaces;

        ResourceRegistry<TextureHandle, GpuTexture> m_textures;
        ResourceRegistry<BufferHandle, GpuBuffer> m_buffers;
        ResourceRegistry<MeshHandle, GpuMesh> m_meshes;

        HandleAllocator<TextureHandle> m_transientHandles;
        ResourceRegistry<TextureHandle, GpuTexture> m_transientTextures;
        HandleAllocator<ShaderHandle> m_shaderHandles;
        ResourceRegistry<ShaderHandle, GpuShader> m_shaders;
        HandleAllocator<PipelineHandle> m_pipelineHandles;
        ResourceRegistry<PipelineHandle, GpuPipeline> m_pipelines;

        uint64_t m_constantUploadCapacity { 0 };
        std::atomic<size_t> m_allocatedMemory { 0 };
    };

} // namespace elm::render::dx12
