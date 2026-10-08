#pragma once

#include "core/Std.hpp"

#include "render/api/HandleAllocator.hpp"
#include "render/rhi/IRenderBackend.hpp"

#if PLATFORM_WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>

namespace elm::render::vulkan {

    struct GpuTexture {
        VkImage image { VK_NULL_HANDLE };
        VkDeviceMemory memory { VK_NULL_HANDLE };
        VkImageView view { VK_NULL_HANDLE };
        TextureDesc desc;
        VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
        size_t memoryBytes { 0 };
    };

    struct GpuBuffer {
        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceMemory memory { VK_NULL_HANDLE };
        void* mapped { nullptr };
        BufferDesc desc;
        size_t memoryBytes { 0 };
    };

    struct GpuMesh {
        VkBuffer vertexBuffer { VK_NULL_HANDLE };
        VkDeviceMemory vertexMemory { VK_NULL_HANDLE };
        VkBuffer indexBuffer { VK_NULL_HANDLE };
        VkDeviceMemory indexMemory { VK_NULL_HANDLE };
        uint32_t indexCount { 0 };
        size_t memoryBytes { 0 };
    };

    struct GpuShader {
        ShaderDesc desc;
        String source;
    };

    struct GpuPipeline {
        struct ConstantSlot {
            uint32_t slot { 0 };
            uint32_t binding { 0 };
            uint32_t size { 0 };
            VkBuffer buffer { VK_NULL_HANDLE };
            VkDeviceMemory memory { VK_NULL_HANDLE };
            void* mapped { nullptr };
        };
        struct TextureSlot {
            uint32_t slot { 0 };
            uint32_t binding { 0 };
            String name;
        };

        VkPipeline pipeline { VK_NULL_HANDLE };
        VkPipelineLayout layout { VK_NULL_HANDLE };
        VkDescriptorSetLayout setLayout { VK_NULL_HANDLE };
        VkDescriptorPool descriptorPool { VK_NULL_HANDLE };
        VkDescriptorSet defaultSet { VK_NULL_HANDLE };
        VkSampler sampler { VK_NULL_HANDLE };
        Vector<ConstantSlot> constants;
        Vector<TextureSlot> textures;
        UnorderedMap<TextureHandle, VkDescriptorSet> textureSets;
        VkFormat colorFormat { VK_FORMAT_UNDEFINED };
        VkFormat depthFormat { VK_FORMAT_UNDEFINED };
        bool hasDepth { false };
        bool scissor { false };
    };

    struct SwapChainImage {
        VkImage image { VK_NULL_HANDLE };
        VkImageView view { VK_NULL_HANDLE };
        VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
    };

    struct VulkanSwapChain {
        VkSwapchainKHR swapchain { VK_NULL_HANDLE };
        VkSurfaceKHR surface { VK_NULL_HANDLE };
        VkFormat colorFormat { VK_FORMAT_B8G8R8A8_SRGB };
        TextureFormat engineColorFormat { TextureFormat::BGRA8_UNORM_SRGB };
        TextureFormat engineDepthFormat { TextureFormat::D32_FLOAT };
        uint32_t width { 0 };
        uint32_t height { 0 };
        Vector<SwapChainImage> images;
        uint32_t imageIndex { 0 };
        bool acquired { false };

        VkImage depthImage { VK_NULL_HANDLE };
        VkDeviceMemory depthMemory { VK_NULL_HANDLE };
        VkImageView depthView { VK_NULL_HANDLE };
        VkImageLayout depthLayout { VK_IMAGE_LAYOUT_UNDEFINED };
        bool needsDepth { false };

        VkSemaphore imageAvailable { VK_NULL_HANDLE };
        VkSemaphore renderFinished { VK_NULL_HANDLE };

        [[nodiscard]] explicit operator bool() const noexcept { return swapchain != VK_NULL_HANDLE; }
    };

    /// Pure Vulkan implementation of the render backend (no Diligent).
    class VulkanBackend final : public rhi::IRenderBackend {
    public:
        VulkanBackend();
        ~VulkanBackend() override;

        VulkanBackend(const VulkanBackend&) = delete;
        VulkanBackend& operator=(const VulkanBackend&) = delete;

        [[nodiscard]] StringView GetName() const override { return "Vulkan"; }
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
        static constexpr uint32_t FramesInFlight = 2;

        struct FrameSync {
            VkCommandPool commandPool { VK_NULL_HANDLE };
            VkCommandBuffer commandBuffer { VK_NULL_HANDLE };
            VkFence inFlight { VK_NULL_HANDLE };
            bool submitted { false };
        };

        [[nodiscard]] GpuTexture* FindTexture(TextureHandle handle);
        [[nodiscard]] const GpuTexture* FindTexture(TextureHandle handle) const;
        [[nodiscard]] VulkanSwapChain* FindSurface(SurfaceId surface);
        [[nodiscard]] const VulkanSwapChain* FindSurface(SurfaceId surface) const;

        [[nodiscard]] GpuTexture CreateNativeTexture(const TextureDesc& desc);
        void DestroyGpuTexture(GpuTexture& texture);
        void DestroyGpuBuffer(GpuBuffer& buffer);
        void DestroyGpuMesh(GpuMesh& mesh);
        void DestroyGpuPipeline(GpuPipeline& pipeline);

        [[nodiscard]] bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
            VkBuffer& buffer, VkDeviceMemory& memory, size_t& outBytes, void** mapped = nullptr);
        void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped, size_t bytes);

        [[nodiscard]] uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
        void CopyToBuffer(VkBuffer dst, const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
        void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout& layout, VkImageLayout newLayout,
            VkImageAspectFlags aspect);

        [[nodiscard]] auto CreateOsSurface(const NativeWindow& window) -> EngineResult<VkSurfaceKHR>;
        [[nodiscard]] auto CreateSwapChain(const NativeWindow& window, uint32_t width, uint32_t height, bool withDepth)
            -> EngineResult<VulkanSwapChain>;
        void DestroySwapChain(VulkanSwapChain& swap);
        void RecreateSwapChain(VulkanSwapChain& swap, const NativeWindow* window, uint32_t width, uint32_t height);
        [[nodiscard]] bool AcquireSwapChainImage(VulkanSwapChain& swap);

        [[nodiscard]] auto CompileHlsl(ShaderStage stage, StringView name, StringView source,
            const Vector<ShaderMacro>& macros, const PipelineDesc& pipeline) -> EngineResult<Vector<uint32_t>>;
        void UploadArena(rhi::UploadArena arena, const rhi::UploadAllocator& uploads);
        [[nodiscard]] bool BeginFrameCommands();
        void SubmitFrameCommands();

        VkInstance m_instance { VK_NULL_HANDLE };
        VkPhysicalDevice m_physicalDevice { VK_NULL_HANDLE };
        VkDevice m_device { VK_NULL_HANDLE };
        VkQueue m_graphicsQueue { VK_NULL_HANDLE };
        uint32_t m_graphicsQueueFamily { 0 };

        VulkanSwapChain m_mainSurface;
        UnorderedMap<SurfaceId, VulkanSwapChain> m_surfaces;
        /// Surfaces acquired during the current Submit; Present consumes them.
        Vector<VulkanSwapChain*> m_acquiredThisFrame;

        FrameSync m_frames[FramesInFlight];
        uint32_t m_frameIndex { 0 };
        VkCommandBuffer m_activeCmd { VK_NULL_HANDLE };
        bool m_frameOpen { false };

        ResourceRegistry<TextureHandle, GpuTexture> m_textures;
        ResourceRegistry<BufferHandle, GpuBuffer> m_buffers;
        ResourceRegistry<MeshHandle, GpuMesh> m_meshes;

        HandleAllocator<TextureHandle> m_transientHandles;
        ResourceRegistry<TextureHandle, GpuTexture> m_transientTextures;
        HandleAllocator<ShaderHandle> m_shaderHandles;
        ResourceRegistry<ShaderHandle, GpuShader> m_shaders;
        HandleAllocator<PipelineHandle> m_pipelineHandles;
        ResourceRegistry<PipelineHandle, GpuPipeline> m_pipelines;

        GpuBuffer m_uploadBuffers[2];

        size_t m_allocatedMemory { 0 };
        String m_dxcPath;
    };

} // namespace elm::render::vulkan
