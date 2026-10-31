#include "Vulkan/Pipeline/VulkanPipeline3D.h"
#include "Core/ErrorLogger.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Every 3D vertex buffer shares one layout: position, normal, color (vec3 each), then uv (vec2)
constexpr uint32_t VERTEX_FLOAT_COUNT = 11;

// mat4 model + vec4 color
constexpr uint32_t MODEL_PUSH_CONSTANT_SIZE = 80;
// mat4 model + vec4 color + 4 floats of icon parameters
constexpr uint32_t CLASS_ICON_PUSH_CONSTANT_SIZE = 96;

struct PipelineOptions {
    bool alphaBlend = false;
    bool depthWrite = true;
};

template <typename Handle, typename Destroy>
void destroyHandle(VkDevice device, Handle& handle, Destroy destroy) {
    if (handle != VK_NULL_HANDLE) {
        destroy(device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}

std::vector<uint32_t> readSpirvFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        AWOW_LOG_ERROR("VULKAN", "Failed to open shader file: " + filename);
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());

    // SPIR-V is a stream of 32-bit words
    if (fileSize % sizeof(uint32_t) != 0) {
        AWOW_LOG_ERROR("VULKAN", "Invalid SPIR-V file (not 4-byte aligned): " + filename);
        return {};
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

// Loads shaders/<name>_vert.spv and shaders/<name>_frag.spv
bool loadShaderPair(VkDevice device, const std::string& name,
                    VkShaderModule& vertModule, VkShaderModule& fragModule) {
    const std::string vertPath = "shaders/" + name + "_vert.spv";
    const std::string fragPath = "shaders/" + name + "_frag.spv";

    auto vertShaderCode = readSpirvFile(vertPath);
    auto fragShaderCode = readSpirvFile(fragPath);

    if (vertShaderCode.empty() || fragShaderCode.empty()) {
        AWOW_LOG_ERROR("VULKAN", "Failed to read shader files - check " + vertPath + " and " + fragPath + " exist");
        return false;
    }

    vertModule = createShaderModule(device, vertShaderCode);
    fragModule = createShaderModule(device, fragShaderCode);

    return vertModule != VK_NULL_HANDLE && fragModule != VK_NULL_HANDLE;
}

// Binding 0 is always the camera uniform; textured pipelines add a sampler at binding 1
bool createDescriptorSetLayout(VkDevice device, bool withTextureSampler, VkDescriptorSetLayout& layout) {
    VkDescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = withTextureSampler ? 2 : 1;
    layoutInfo.pBindings = bindings;

    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) == VK_SUCCESS;
}

bool createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                          VkShaderStageFlags pushConstantStages, uint32_t pushConstantSize,
                          VkPipelineLayout& layout) {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = pushConstantStages;
    pushConstantRange.offset = 0;
    pushConstantRange.size = pushConstantSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    return vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &layout) == VK_SUCCESS;
}

// Triangle list with dynamic viewport/scissor and no face culling: actor shapes are
// billboarded, the floor is seen from both sides, and OBJ winding is not consistent.
bool createGraphicsPipeline(VkDevice device, VkRenderPass renderPass, uint32_t subpass,
                            VkShaderModule vertModule, VkShaderModule fragModule,
                            VkPipelineLayout layout, const PipelineOptions& options,
                            VkPipeline& pipeline) {
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * VERTEX_FLOAT_COUNT;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    const VkVertexInputAttributeDescription attributeDescriptions[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},                  // position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},  // normal
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6},  // color
        {3, 0, VK_FORMAT_R32G32_SFLOAT,    sizeof(float) * 9},  // uv
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attributeDescriptions));
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = options.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = options.alphaBlend ? VK_TRUE : VK_FALSE;
    if (options.alphaBlend) {
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;
}

bool reportFailure(const char* step, const char* pipelineName) {
    AWOW_LOG_ERROR("VULKAN", std::string("Failed to ") + step + " for the " + pipelineName + " pipeline");
    return false;
}

} // namespace

bool VulkanPipeline3D::initialize(VkDevice device, VkRenderPass renderPass, uint32_t subpass) {
    constexpr VkShaderStageFlags vertexStage = VK_SHADER_STAGE_VERTEX_BIT;
    constexpr VkShaderStageFlags vertexAndFragment = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Actor shapes. Blended so actors can be drawn translucent (Play Along shows friendly players at 50%)
    if (!createDescriptorSetLayout(device, false, descriptorSetLayout))
        return reportFailure("create the descriptor set layout", "sphere");
    if (!createPipelineLayout(device, descriptorSetLayout, vertexStage, MODEL_PUSH_CONSTANT_SIZE, pipelineLayout))
        return reportFailure("create the pipeline layout", "sphere");
    if (!loadShaderPair(device, "sphere", vertShaderModule, fragShaderModule))
        return reportFailure("load shaders", "sphere");
    if (!createGraphicsPipeline(device, renderPass, subpass, vertShaderModule, fragShaderModule,
                                pipelineLayout, {.alphaBlend = true}, pipeline))
        return reportFailure("create the graphics pipeline", "sphere");

    // Textured floor
    if (!createDescriptorSetLayout(device, true, floorDescriptorSetLayout))
        return reportFailure("create the descriptor set layout", "floor");
    if (!createPipelineLayout(device, floorDescriptorSetLayout, vertexAndFragment, MODEL_PUSH_CONSTANT_SIZE, floorPipelineLayout))
        return reportFailure("create the pipeline layout", "floor");
    if (!loadShaderPair(device, "floor", floorVertShaderModule, floorFragShaderModule))
        return reportFailure("load shaders", "floor");
    if (!createGraphicsPipeline(device, renderPass, subpass, floorVertShaderModule, floorFragShaderModule,
                                floorPipelineLayout, {}, floorPipeline))
        return reportFailure("create the graphics pipeline", "floor");

    // Selection ring shares the actor descriptor set layout. Depth-tested but not
    // written, so the ring never hides other objects
    if (!createPipelineLayout(device, descriptorSetLayout, vertexStage, MODEL_PUSH_CONSTANT_SIZE, selectionRingPipelineLayout))
        return reportFailure("create the pipeline layout", "selection ring");
    if (!loadShaderPair(device, "selection_ring", selectionRingVertShaderModule, selectionRingFragShaderModule))
        return reportFailure("load shaders", "selection ring");
    if (!createGraphicsPipeline(device, renderPass, subpass, selectionRingVertShaderModule, selectionRingFragShaderModule,
                                selectionRingPipelineLayout, {.alphaBlend = true, .depthWrite = false}, selectionRingPipeline))
        return reportFailure("create the graphics pipeline", "selection ring");

    // Class icon discs
    if (!createDescriptorSetLayout(device, true, classIconDescriptorSetLayout))
        return reportFailure("create the descriptor set layout", "class icon");
    if (!createPipelineLayout(device, classIconDescriptorSetLayout, vertexAndFragment, CLASS_ICON_PUSH_CONSTANT_SIZE, classIconPipelineLayout))
        return reportFailure("create the pipeline layout", "class icon");
    if (!loadShaderPair(device, "classicon", classIconVertShaderModule, classIconFragShaderModule))
        return reportFailure("load shaders", "class icon");
    if (!createGraphicsPipeline(device, renderPass, subpass, classIconVertShaderModule, classIconFragShaderModule,
                                classIconPipelineLayout, {}, classIconPipeline))
        return reportFailure("create the graphics pipeline", "class icon");

    // Custom OBJ models. Blended for textures with transparency
    if (!createDescriptorSetLayout(device, true, texturedModelDescriptorSetLayout))
        return reportFailure("create the descriptor set layout", "textured model");
    if (!createPipelineLayout(device, texturedModelDescriptorSetLayout, vertexStage, MODEL_PUSH_CONSTANT_SIZE, texturedModelPipelineLayout))
        return reportFailure("create the pipeline layout", "textured model");
    if (!loadShaderPair(device, "textured_model", texturedModelVertShaderModule, texturedModelFragShaderModule))
        return reportFailure("load shaders", "textured model");
    if (!createGraphicsPipeline(device, renderPass, subpass, texturedModelVertShaderModule, texturedModelFragShaderModule,
                                texturedModelPipelineLayout, {.alphaBlend = true}, texturedModelPipeline))
        return reportFailure("create the graphics pipeline", "textured model");

    return true;
}

void VulkanPipeline3D::cleanup(VkDevice device) {
    destroyHandle(device, pipeline, vkDestroyPipeline);
    destroyHandle(device, pipelineLayout, vkDestroyPipelineLayout);
    destroyHandle(device, descriptorSetLayout, vkDestroyDescriptorSetLayout);
    destroyHandle(device, vertShaderModule, vkDestroyShaderModule);
    destroyHandle(device, fragShaderModule, vkDestroyShaderModule);

    destroyHandle(device, floorPipeline, vkDestroyPipeline);
    destroyHandle(device, floorPipelineLayout, vkDestroyPipelineLayout);
    destroyHandle(device, floorDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    destroyHandle(device, floorVertShaderModule, vkDestroyShaderModule);
    destroyHandle(device, floorFragShaderModule, vkDestroyShaderModule);

    destroyHandle(device, selectionRingPipeline, vkDestroyPipeline);
    destroyHandle(device, selectionRingPipelineLayout, vkDestroyPipelineLayout);
    destroyHandle(device, selectionRingVertShaderModule, vkDestroyShaderModule);
    destroyHandle(device, selectionRingFragShaderModule, vkDestroyShaderModule);

    destroyHandle(device, classIconPipeline, vkDestroyPipeline);
    destroyHandle(device, classIconPipelineLayout, vkDestroyPipelineLayout);
    destroyHandle(device, classIconDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    destroyHandle(device, classIconVertShaderModule, vkDestroyShaderModule);
    destroyHandle(device, classIconFragShaderModule, vkDestroyShaderModule);

    destroyHandle(device, texturedModelPipeline, vkDestroyPipeline);
    destroyHandle(device, texturedModelPipelineLayout, vkDestroyPipelineLayout);
    destroyHandle(device, texturedModelDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    destroyHandle(device, texturedModelVertShaderModule, vkDestroyShaderModule);
    destroyHandle(device, texturedModelFragShaderModule, vkDestroyShaderModule);
}
