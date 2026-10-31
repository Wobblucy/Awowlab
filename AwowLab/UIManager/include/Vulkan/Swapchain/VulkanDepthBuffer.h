#pragma once
#include <vulkan/vulkan.h>

class VulkanDepthBuffer {
public:
    VulkanDepthBuffer();
    ~VulkanDepthBuffer();

    VulkanDepthBuffer(const VulkanDepthBuffer&) = delete;
    VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;

    bool initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                   uint32_t width, uint32_t height);

    void cleanup(VkDevice device);

    VkImage getImage() const { return depthImage; }
    VkImageView getImageView() const { return depthImageView; }
    VkFormat getFormat() const { return depthFormat; }

private:
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    VkFormat depthFormat;

    VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);
    bool hasStencilComponent(VkFormat format);
};
