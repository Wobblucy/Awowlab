#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class VulkanCommands {
public:
    VulkanCommands();
    ~VulkanCommands();

    VulkanCommands(const VulkanCommands&) = delete;
    VulkanCommands& operator=(const VulkanCommands&) = delete;

    bool initialize(VkDevice device, uint32_t queueFamilyIndex, size_t frameCount);
    void cleanup(VkDevice device);

    VkCommandPool getCommandPool() const { return commandPool; }
    const std::vector<VkCommandBuffer>& getCommandBuffers() const { return commandBuffers; }
    VkCommandBuffer getCommandBuffer(size_t index) const { return commandBuffers[index]; }

    VkCommandBuffer beginSingleTimeCommands(VkDevice device);
    void endSingleTimeCommands(VkDevice device, VkCommandBuffer commandBuffer, VkQueue queue);

private:
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    bool createCommandPool(VkDevice device, uint32_t queueFamilyIndex);
    bool createCommandBuffers(VkDevice device, size_t frameCount);
};
