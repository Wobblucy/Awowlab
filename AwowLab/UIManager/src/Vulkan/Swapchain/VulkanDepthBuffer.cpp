#include "Vulkan/Swapchain/VulkanDepthBuffer.h"
#include "Core/ErrorLogger.h"
#include "Vulkan/Buffers/VulkanBufferUtils.h"
#include <stdexcept>

VulkanDepthBuffer::VulkanDepthBuffer()
    : depthImage(VK_NULL_HANDLE)
    , depthImageMemory(VK_NULL_HANDLE)
    , depthImageView(VK_NULL_HANDLE)
    , depthFormat(VK_FORMAT_UNDEFINED)
{
}

VulkanDepthBuffer::~VulkanDepthBuffer() {
}

VkFormat VulkanDepthBuffer::findDepthFormat(VkPhysicalDevice physicalDevice) {
    // Try formats in order of preference
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format");
}

bool VulkanDepthBuffer::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanDepthBuffer::initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                                   uint32_t width, uint32_t height) {
    depthFormat = findDepthFormat(physicalDevice);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device, &imageInfo, nullptr, &depthImage);
    if (result != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create depth image - VkResult: " + std::to_string(static_cast<int>(result)));
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, depthImage, &memRequirements);

    auto memoryType = vulkan_buffers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memoryType) {
        AWOW_LOG_ERROR("VULKAN", "No device-local memory type available for the depth image");
        return false;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = *memoryType;

    result = vkAllocateMemory(device, &allocInfo, nullptr, &depthImageMemory);
    if (result != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to allocate depth image memory - VkResult: " + std::to_string(static_cast<int>(result)));
        return false;
    }

    vkBindImageMemory(device, depthImage, depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device, &viewInfo, nullptr, &depthImageView);
    if (result != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create depth image view - VkResult: " + std::to_string(static_cast<int>(result)));
        return false;
    }

    return true;
}

void VulkanDepthBuffer::cleanup(VkDevice device) {
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }

    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }

    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }
}
