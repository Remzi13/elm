#include "render/backend/vulkan/VulkanPipeline.hpp"

#include "core/Log.hpp"

#include "render/backend/vulkan/VulkanDevice.hpp"
#include "render/backend/vulkan/VulkanFormats.hpp"
#include "render/backend/vulkan/VulkanShader.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace elm::render::vulkan {

namespace {

    constexpr const char* Source = "VulkanBackend";
    constexpr std::array DynamicStates { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    SpirvBindings BindingsOf(const PipelineDesc& desc)
    {
        SpirvBindings bindings;
        for (uint32_t slot = 0; slot < desc.bindings.size(); ++slot)
            bindings[desc.bindings[slot].name] = slot;
        for (uint32_t index = 0; index < desc.samplers.size(); ++index)
            bindings[desc.samplers[index].textureName + "_sampler"] = static_cast<uint32_t>(desc.bindings.size()) + index;
        return bindings;
    }

    VkDescriptorSetLayout CreateSetLayout(VkDevice device, const PipelineDesc& desc, const VkSampler* sampler)
    {
        Vector<VkDescriptorSetLayoutBinding> bindings;
        for (uint32_t slot = 0; slot < desc.bindings.size(); ++slot) {
            const auto& binding = desc.bindings[slot];
            const VkDescriptorType type = binding.type == BindingType::Constants ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                                                 : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            bindings.push_back({ slot, type, 1, ToShaderStage(binding.stage), nullptr });
        }
        for (uint32_t index = 0; index < desc.samplers.size(); ++index) {
            const uint32_t binding = static_cast<uint32_t>(desc.bindings.size()) + index;
            bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_SAMPLER, 1, ToShaderStage(desc.samplers[index].stage), sampler });
        }

        VkDescriptorSetLayoutCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);
        return layout;
    }

    VkShaderModule CreateModule(VkDevice device, const SpirvShader& shader, const SpirvBindings& bindings)
    {
        const Vector<uint32_t> words = RemapBindings(shader.words, bindings);
        VkShaderModuleCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = words.size() * sizeof(uint32_t);
        info.pCode = words.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
            ERROR_MESSAGE(log::Category::Render, Source, "Failed to create shader module '%s'", shader.name.c_str());
        return module;
    }

    /// Attributes in each vertex buffer slot are packed in declaration order.
    struct VertexInput {
        Vector<VkVertexInputBindingDescription> bindings;
        Vector<VkVertexInputAttributeDescription> attributes;
        VkPipelineVertexInputStateCreateInfo state {};

        explicit VertexInput(const Vector<VertexAttribute>& layout)
        {
            state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            for (const auto& attribute : layout) {
                if (attribute.bufferSlot >= bindings.size())
                    bindings.resize(attribute.bufferSlot + 1);
                auto& binding = bindings[attribute.bufferSlot];
                binding.binding = attribute.bufferSlot;
                binding.inputRate = attribute.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
                attributes.push_back({ attribute.location, attribute.bufferSlot, ToVkFormat(attribute.format), binding.stride });
                binding.stride += VertexFormatSize(attribute.format);
            }
            state.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
            state.pVertexBindingDescriptions = bindings.data();
            state.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            state.pVertexAttributeDescriptions = attributes.data();
        }
    };

    VkPipelineColorBlendAttachmentState BlendState(BlendMode mode)
    {
        VkPipelineColorBlendAttachmentState blend {};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (mode == BlendMode::Opaque)
            return blend;
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = mode == BlendMode::AlphaBlend ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        return blend;
    }

    VkCullModeFlags CullModeOf(CullMode mode)
    {
        switch (mode) {
        case CullMode::Back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::Front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::None:
        default:
            return VK_CULL_MODE_NONE;
        }
    }

    /// Fixed-function state of a pipeline; the structures point into each other, so they live together.
    struct FixedState {
        VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
        VkPipelineViewportStateCreateInfo viewport {};
        VkPipelineRasterizationStateCreateInfo rasterization {};
        VkPipelineMultisampleStateCreateInfo multisample {};
        VkPipelineDepthStencilStateCreateInfo depthStencil {};
        VkPipelineColorBlendAttachmentState blendAttachment {};
        VkPipelineColorBlendStateCreateInfo blend {};
        VkPipelineDynamicStateCreateInfo dynamic {};
        VkFormat colorFormat { VK_FORMAT_UNDEFINED };
        VkPipelineRenderingCreateInfo rendering {};

        explicit FixedState(const PipelineDesc& desc)
        {
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            inputAssembly.topology = desc.topology == PrimitiveTopology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                                   : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            rasterization.polygonMode = VK_POLYGON_MODE_FILL;
            rasterization.cullMode = CullModeOf(desc.cull);
            // Clockwise front faces as in D3D; the flipped viewport keeps the winding of D3D-style geometry
            rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterization.lineWidth = 1.0f;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            depthStencil.depthTestEnable = desc.depthTest;
            depthStencil.depthWriteEnable = desc.depthWrite;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
            blendAttachment = BlendState(desc.blend);
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;
            dynamic.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
            dynamic.pDynamicStates = DynamicStates.data();

            colorFormat = ToVkFormat(desc.colorFormat);
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &colorFormat;
            rendering.depthAttachmentFormat = ToVkFormat(desc.depthFormat);
            if (desc.depthFormat == TextureFormat::D24_UNORM_S8_UINT)
                rendering.stencilAttachmentFormat = rendering.depthAttachmentFormat;
        }
    };

} // namespace

VulkanPipeline VulkanPipeline::Create(VulkanDevice& device, const VulkanPipelineCreateInfo& info)
{
    const PipelineDesc& desc = info.desc;
    VulkanPipeline pipeline;
    pipeline.m_device = device.Get();
    pipeline.m_scissor = desc.scissor;
    for (const auto& binding : desc.bindings)
        pipeline.m_slots.push_back({ binding.type, binding.size });
    for (const auto& attribute : desc.vertexLayout)
        pipeline.m_vertexBufferCount = (std::max)(pipeline.m_vertexBufferCount, attribute.bufferSlot + 1);

    pipeline.m_setLayout = CreateSetLayout(device.Get(), desc, &info.sampler);
    VkPipelineLayoutCreateInfo layoutInfo {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pipeline.m_setLayout;
    if (pipeline.m_setLayout == VK_NULL_HANDLE || vkCreatePipelineLayout(device.Get(), &layoutInfo, nullptr, &pipeline.m_layout) != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Failed to create the layout of pipeline '%s'", desc.name.c_str());
        return {};
    }

    const SpirvBindings bindings = BindingsOf(desc);
    const std::array modules { CreateModule(device.Get(), info.vertexShader, bindings), CreateModule(device.Get(), info.pixelShader, bindings) };
    std::array<VkPipelineShaderStageCreateInfo, 2> stages {};
    for (size_t stage = 0; stage < stages.size(); ++stage) {
        stages[stage].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[stage].stage = stage == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[stage].module = modules[stage];
        stages[stage].pName = "main";
    }

    const VertexInput vertexInput(desc.vertexLayout);
    const FixedState state(desc);
    VkGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &state.rendering;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput.state;
    pipelineInfo.pInputAssemblyState = &state.inputAssembly;
    pipelineInfo.pViewportState = &state.viewport;
    pipelineInfo.pRasterizationState = &state.rasterization;
    pipelineInfo.pMultisampleState = &state.multisample;
    pipelineInfo.pDepthStencilState = &state.depthStencil;
    pipelineInfo.pColorBlendState = &state.blend;
    pipelineInfo.pDynamicState = &state.dynamic;
    pipelineInfo.layout = pipeline.m_layout;

    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (modules[0] != VK_NULL_HANDLE && modules[1] != VK_NULL_HANDLE)
        result = vkCreateGraphicsPipelines(device.Get(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.m_pipeline);
    for (const VkShaderModule module : modules)
        vkDestroyShaderModule(device.Get(), module, nullptr);
    if (result != VK_SUCCESS) {
        ERROR_MESSAGE(log::Category::Render, Source, "Failed to create pipeline '%s'", desc.name.c_str());
        return {};
    }
    return pipeline;
}

VulkanPipeline::~VulkanPipeline()
{
    Release();
}

VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_setLayout(std::exchange(other.m_setLayout, VK_NULL_HANDLE))
    , m_slots(std::move(other.m_slots))
    , m_vertexBufferCount(std::exchange(other.m_vertexBufferCount, 0))
    , m_scissor(std::exchange(other.m_scissor, false))
{
}

VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept
{
    if (this != &other) {
        Release();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_setLayout = std::exchange(other.m_setLayout, VK_NULL_HANDLE);
        m_slots = std::move(other.m_slots);
        m_vertexBufferCount = std::exchange(other.m_vertexBufferCount, 0);
        m_scissor = std::exchange(other.m_scissor, false);
    }
    return *this;
}

void VulkanPipeline::Release() noexcept
{
    if (m_device == VK_NULL_HANDLE)
        return;
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_layout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}

} // namespace elm::render::vulkan
