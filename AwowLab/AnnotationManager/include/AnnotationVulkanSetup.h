#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct AnnotationVertex;

namespace annotation_vulkan {

// One binding: the camera UBO
bool createDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout& outLayout);

bool createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                          VkPipelineLayout& outLayout);

std::vector<uint32_t> readShaderFile(const std::string& filename);
VkShaderModule createShaderModule(VkDevice device, const std::vector<uint32_t>& code);

bool loadShaders(VkDevice device, const std::string& vertPath, const std::string& fragPath,
                 VkShaderModule& outVertModule, VkShaderModule& outFragModule);

bool createGraphicsPipeline(VkDevice device, VkRenderPass renderPass, uint32_t subpass,
                            VkPipelineLayout pipelineLayout,
                            VkShaderModule vertModule, VkShaderModule fragModule,
                            VkPipeline& outPipeline);

bool createDescriptorPool(VkDevice device, VkDescriptorPool& outPool);
bool createDescriptorSet(VkDevice device, VkDescriptorPool pool,
                         VkDescriptorSetLayout layout, VkDescriptorSet& outSet);

bool createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer, VkDeviceMemory& bufferMemory);

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);

} // namespace annotation_vulkan
