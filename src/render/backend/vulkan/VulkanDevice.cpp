#include "render/backend/vulkan/VulkanDevice.hpp"

#include "core/Log.hpp"
#include "core/Unexpected.hpp"

#include "render/backend/vulkan/VulkanSurface.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>

namespace elm::render::vulkan {

    namespace {

        constexpr const char* Source = "VulkanBackend";
        constexpr uint32_t RequiredApiVersion = VK_API_VERSION_1_3;
        constexpr const char* ValidationLayer = "VK_LAYER_KHRONOS_validation";
        constexpr std::array DeviceExtensions { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };

#if defined(NDEBUG)
        constexpr bool UseValidation = false;
#else
        constexpr bool UseValidation = true;
#endif

        auto InitError(const char* message, VkResult result = VK_SUCCESS) -> std::unexpected<EngineError>
        {
            String text(message);
            if (result != VK_SUCCESS)
                text += " (VkResult " + String(std::to_string(static_cast<int>(result))) + ")";
            return MakeUnexpected(ErrorCode::RenderEngineInitializationFailed, text, log::Category::Render, Source);
        }

        bool HasLayer(const char* name)
        {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            Vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            return std::ranges::any_of(layers, [name](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
        }

        bool HasInstanceExtension(const char* name)
        {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            Vector<VkExtensionProperties> extensions(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
            return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
        }

        bool HasDeviceExtensions(VkPhysicalDevice device)
        {
            uint32_t count = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
            Vector<VkExtensionProperties> extensions(count);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
            return std::ranges::all_of(DeviceExtensions, [&](const char* required) {
                return std::ranges::any_of(extensions, [required](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, required) == 0;
                });
            });
        }

        bool HasRequiredFeatures(VkPhysicalDevice device)
        {
            VkPhysicalDeviceVulkan13Features features13 {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 features {};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(device, &features);
            return features13.dynamicRendering && features13.synchronization2;
        }

        /// Graphics queue family that can present to the surface.
        std::optional<uint32_t> FindQueueFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
            Vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
            for (uint32_t family = 0; family < count; ++family) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &present);
                if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                    return family;
            }
            return std::nullopt;
        }

        /// Higher is better; discrete GPUs first.
        int DeviceScore(const VkPhysicalDeviceProperties& properties)
        {
            switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                return 3;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                return 2;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                return 1;
            default:
                return 0;
            }
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL OnValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                ERROR_MESSAGE(log::Category::Render, "Vulkan", "%s", data->pMessage);
            else
                WARNING_MESSAGE(log::Category::Render, "Vulkan", "%s", data->pMessage);
            return VK_FALSE;
        }

        VkDebugUtilsMessengerCreateInfoEXT MessengerInfo()
        {
            VkDebugUtilsMessengerCreateInfoEXT info {};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            info.pfnUserCallback = OnValidationMessage;
            return info;
        }

    } // namespace

    VulkanDevice::~VulkanDevice()
    {
        Shutdown();
    }

    auto VulkanDevice::Init(const NativeWindow& window) -> EngineResult<void>
    {
        if (auto result = CreateInstance(window.system); !result)
            return result;

        // A probe surface finds a GPU that presents to this window; the swapchain creates its own
        const VkSurfaceKHR surface = CreateNativeSurface(m_instance, window);
        if (surface == VK_NULL_HANDLE)
            return InitError("Failed to create a surface for the main window");
        auto selected = SelectPhysicalDevice(surface);
        vkDestroySurfaceKHR(m_instance, surface, nullptr);
        if (!selected)
            return selected;

        return CreateLogicalDevice();
    }

    void VulkanDevice::Shutdown()
    {
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_debugMessenger != VK_NULL_HANDLE) {
            const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy)
                destroy(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        m_physicalDevice = VK_NULL_HANDLE;
        m_queue = VK_NULL_HANDLE;
        m_pushDescriptorSet = nullptr;
    }

    auto VulkanDevice::CreateInstance(WindowSystem system) -> EngineResult<void>
    {
        const char* surfaceExtension = SurfaceExtensionName(system);
        if (!surfaceExtension || !HasInstanceExtension(surfaceExtension))
            return InitError("The Vulkan loader cannot create surfaces for this window system");

        Vector<const char*> extensions { VK_KHR_SURFACE_EXTENSION_NAME, surfaceExtension };
        Vector<const char*> layers;
        const bool validation = UseValidation && HasLayer(ValidationLayer) && HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (validation) {
            layers.push_back(ValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkApplicationInfo application {};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "elm";
        application.pEngineName = "elm";
        application.apiVersion = RequiredApiVersion;

        const auto messenger = MessengerInfo();
        VkInstanceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pNext = validation ? &messenger : nullptr;
        info.pApplicationInfo = &application;
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.data();
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (const VkResult result = vkCreateInstance(&info, nullptr, &m_instance); result != VK_SUCCESS)
            return InitError("Failed to create the Vulkan instance", result);

        if (validation) {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
            if (create)
                create(m_instance, &messenger, nullptr, &m_debugMessenger);
        }
        return {};
    }

    auto VulkanDevice::SelectPhysicalDevice(VkSurfaceKHR surface) -> EngineResult<void>
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        Vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

        int bestScore = -1;
        for (const VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            const auto family = FindQueueFamily(device, surface);
            if (properties.apiVersion < RequiredApiVersion || !family || !HasDeviceExtensions(device) || !HasRequiredFeatures(device))
                continue;
            if (DeviceScore(properties) <= bestScore)
                continue;
            bestScore = DeviceScore(properties);
            m_physicalDevice = device;
            m_properties = properties;
            m_queueFamily = *family;
        }
        if (m_physicalDevice == VK_NULL_HANDLE)
            return InitError("No GPU supports Vulkan 1.3 with dynamic rendering, push descriptors and presentation to the window");

        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
        LOG_MESSAGE(log::Category::Render, Source, "GPU: %s", m_properties.deviceName);
        return {};
    }

    auto VulkanDevice::CreateLogicalDevice() -> EngineResult<void>
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue {};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = m_queueFamily;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features features13 {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        VkDeviceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.pNext = &features13;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
        info.ppEnabledExtensionNames = DeviceExtensions.data();
        if (const VkResult result = vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device); result != VK_SUCCESS)
            return InitError("Failed to create the Vulkan device", result);

        vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
        m_pushDescriptorSet = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(m_device, "vkCmdPushDescriptorSetKHR"));
        if (!m_pushDescriptorSet)
            return InitError("vkCmdPushDescriptorSetKHR is not available");
        return {};
    }

    VkDeviceMemory VulkanDevice::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties)
    {
        for (uint32_t type = 0; type < m_memoryProperties.memoryTypeCount; ++type) {
            const bool allowed = requirements.memoryTypeBits & (1u << type);
            if (!allowed || (m_memoryProperties.memoryTypes[type].propertyFlags & properties) != properties)
                continue;

            VkMemoryAllocateInfo info {};
            info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            info.allocationSize = requirements.size;
            info.memoryTypeIndex = type;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            if (vkAllocateMemory(m_device, &info, nullptr, &memory) != VK_SUCCESS)
                return VK_NULL_HANDLE;
            m_allocatedBytes.fetch_add(static_cast<size_t>(requirements.size), std::memory_order_relaxed);
            return memory;
        }
        return VK_NULL_HANDLE;
    }

    void VulkanDevice::FreeMemory(VkDeviceMemory memory, VkDeviceSize size) noexcept
    {
        if (memory == VK_NULL_HANDLE)
            return;
        vkFreeMemory(m_device, memory, nullptr);
        m_allocatedBytes.fetch_sub(static_cast<size_t>(size), std::memory_order_relaxed);
    }

    void VulkanDevice::PushDescriptorSet(VkCommandBuffer commands, VkPipelineLayout layout, std::span<const VkWriteDescriptorSet> writes) const
    {
        if (!writes.empty())
            m_pushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, static_cast<uint32_t>(writes.size()), writes.data());
    }

    void VulkanDevice::WaitIdle() const
    {
        if (m_device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_device);
    }

} // namespace elm::render::vulkan
