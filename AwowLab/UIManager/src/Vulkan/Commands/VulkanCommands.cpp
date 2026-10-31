#include "Vulkan/Commands/VulkanCommands.h"
#include "Core/ErrorLogger.h"

VulkanCommands::VulkanCommands()
    : commandPool(VK_NULL_HANDLE)
{
}

VulkanCommands::~VulkanCommands() {
}

bool VulkanCommands::initialize(VkDevice device, uint32_t queueFamilyIndex, size_t frameCount) {
    if (!createCommandPool(device, queueFamilyIndex)) {
        return false;
    }

    if (!createCommandBuffers(device, frameCount)) {
        return false;
    }

    return true;
}

bool VulkanCommands::createCommandPool(VkDevice device, uint32_t queueFamilyIndex) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create command pool");
        return false;
    }

    return true;
}

bool VulkanCommands::createCommandBuffers(VkDevice device, size_t frameCount) {
    commandBuffers.resize(frameCount);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to allocate command buffers");
        return false;
    }

    return true;
}

VkCommandBuffer VulkanCommands::beginSingleTimeCommands(VkDevice device) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanCommands::endSingleTimeCommands(VkDevice device, VkCommandBuffer commandBuffer, VkQueue queue) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanCommands::cleanup(VkDevice device) {
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }
    commandBuffers.clear();
}
