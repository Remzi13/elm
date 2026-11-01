#pragma once

#include "core/Error.hpp"
#include "core/Std.hpp"

#include "render/api/NativeWindow.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <span>

namespace elm::render::vulkan {

    /// Vulkan instance, the selected GPU, the logical device and its graphics queue (which also presents).
    /// Requires Vulkan 1.3 (dynamic rendering, synchronization2) and VK_KHR_push_descriptor.
    ///
    /// Owned by VulkanBackend, used on the render thread; Init/Shutdown while the render thread is stopped.
    /// GetAllocatedBytes may be read from any thread.
    class VulkanDevice {
    public:
        VulkanDevice() = default;
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        VulkanDevice& operator=(VulkanDevice&&) = delete;

        /// The window selects the surface extension and a GPU that can present to it.
        [[nodiscard]] auto Init(const NativeWindow& window) -> EngineResult<void>;
        void Shutdown();

        [[nodiscard]] bool IsValid() const noexcept { return m_device != VK_NULL_HANDLE; }
        [[nodiscard]] VkInstance GetInstance() const noexcept { return m_instance; }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept { return m_physicalDevice; }
        [[nodiscard]] VkDevice Get() const noexcept { return m_device; }
        [[nodiscard]] VkQueue GetQueue() const noexcept { return m_queue; }
        [[nodiscard]] uint32_t GetQueueFamily() const noexcept { return m_queueFamily; }
        [[nodiscard]] const VkPhysicalDeviceLimits& GetLimits() const noexcept { return m_properties.limits; }

        /// Memory of a type allowed by requirements with the given properties. VK_NULL_HANDLE on failure.
        [[nodiscard]] VkDeviceMemory AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties);
        void FreeMemory(VkDeviceMemory memory, VkDeviceSize size) noexcept;
        [[nodiscard]] size_t GetAllocatedBytes() const noexcept { return m_allocatedBytes.load(std::memory_order_relaxed); }

        void PushDescriptorSet(VkCommandBuffer commands, VkPipelineLayout layout, std::span<const VkWriteDescriptorSet> writes) const;
        void WaitIdle() const;

    private:
        [[nodiscard]] auto CreateInstance(WindowSystem system) -> EngineResult<void>;
        [[nodiscard]] auto SelectPhysicalDevice(VkSurfaceKHR surface) -> EngineResult<void>;
        [[nodiscard]] auto CreateLogicalDevice() -> EngineResult<void>;

        VkInstance m_instance { VK_NULL_HANDLE };
        VkDebugUtilsMessengerEXT m_debugMessenger { VK_NULL_HANDLE };
        VkPhysicalDevice m_physicalDevice { VK_NULL_HANDLE };
        VkPhysicalDeviceProperties m_properties {};
        VkPhysicalDeviceMemoryProperties m_memoryProperties {};
        VkDevice m_device { VK_NULL_HANDLE };
        VkQueue m_queue { VK_NULL_HANDLE };
        uint32_t m_queueFamily { 0 };
        PFN_vkCmdPushDescriptorSetKHR m_pushDescriptorSet { nullptr };
        std::atomic<size_t> m_allocatedBytes { 0 };
    };

} // namespace elm::render::vulkan
