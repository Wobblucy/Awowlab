#pragma once
#include <vulkan/vulkan.h>

// Owns the 3D pipelines: actor shapes, floor, selection ring, class icons and textured models
class VulkanPipeline3D {
public:
    VulkanPipeline3D() = default;
    ~VulkanPipeline3D() = default;

    bool initialize(VkDevice device, VkRenderPass renderPass, uint32_t subpass = 0);

    void cleanup(VkDevice device);

    // Getters for sphere pipeline (actors)
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    // Getters for floor pipeline (textured floor)
    VkPipeline getFloorPipeline() const { return floorPipeline; }
    VkPipelineLayout getFloorPipelineLayout() const { return floorPipelineLayout; }
    VkDescriptorSetLayout getFloorDescriptorSetLayout() const { return floorDescriptorSetLayout; }

    // Getters for selection ring pipeline (alpha-blended ring effect)
    VkPipeline getSelectionRingPipeline() const { return selectionRingPipeline; }
    VkPipelineLayout getSelectionRingPipelineLayout() const { return selectionRingPipelineLayout; }

    // Getters for class icon pipeline (textured actor discs with spec icons)
    VkPipeline getClassIconPipeline() const { return classIconPipeline; }
    VkPipelineLayout getClassIconPipelineLayout() const { return classIconPipelineLayout; }
    VkDescriptorSetLayout getClassIconDescriptorSetLayout() const { return classIconDescriptorSetLayout; }

    // Getters for textured model pipeline (custom OBJ models with MTL textures)
    VkPipeline getTexturedModelPipeline() const { return texturedModelPipeline; }
    VkPipelineLayout getTexturedModelPipelineLayout() const { return texturedModelPipelineLayout; }
    VkDescriptorSetLayout getTexturedModelDescriptorSetLayout() const { return texturedModelDescriptorSetLayout; }

private:
    // Sphere pipeline (for actors)
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;

    // Floor pipeline (for textured floor)
    VkPipeline floorPipeline = VK_NULL_HANDLE;
    VkPipelineLayout floorPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout floorDescriptorSetLayout = VK_NULL_HANDLE;
    VkShaderModule floorVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule floorFragShaderModule = VK_NULL_HANDLE;

    // Selection ring pipeline (alpha-blended for pulsing ring effect)
    VkPipeline selectionRingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout selectionRingPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule selectionRingVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule selectionRingFragShaderModule = VK_NULL_HANDLE;

    // Class icon pipeline (textured disc for spec icons)
    VkPipeline classIconPipeline = VK_NULL_HANDLE;
    VkPipelineLayout classIconPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout classIconDescriptorSetLayout = VK_NULL_HANDLE;
    VkShaderModule classIconVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule classIconFragShaderModule = VK_NULL_HANDLE;

    // Textured model pipeline (custom OBJ models with MTL textures)
    VkPipeline texturedModelPipeline = VK_NULL_HANDLE;
    VkPipelineLayout texturedModelPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout texturedModelDescriptorSetLayout = VK_NULL_HANDLE;
    VkShaderModule texturedModelVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule texturedModelFragShaderModule = VK_NULL_HANDLE;
};
