#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <optional>

// Small helpers shared by everything that owns a plain VkBuffer + VkDeviceMemory pair
namespace vulkan_buffers {

// First memory type allowed by typeFilter that has every requested property
inline std::optional<uint32_t> findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                              VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return std::nullopt;
}

inline void destroyBuffer(VkDevice device, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

// Creates a buffer with its own bound allocation. On failure both handles are left null.
inline bool createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkBuffer& buffer, VkDeviceMemory& memory) {
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties).value_or(0);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        memory = VK_NULL_HANDLE;
        destroyBuffer(device, buffer, memory);
        return false;
    }

    vkBindBufferMemory(device, buffer, memory, 0);
    return true;
}

// Host-visible, coherent buffer holding a copy of `data`. On failure both handles are left null.
inline bool createFilledBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkBufferUsageFlags usage, const void* data, VkDeviceSize size,
                               VkBuffer& buffer, VkDeviceMemory& memory) {
    if (!createBuffer(device, physicalDevice, size, usage,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buffer, memory)) {
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device, memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        destroyBuffer(device, buffer, memory);
        return false;
    }
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device, memory);
    return true;
}

} // namespace vulkan_buffers
