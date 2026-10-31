#include "Vulkan/Core/VulkanDebug.h"

PFN_vkSetDebugUtilsObjectNameEXT VulkanDebug::setObjectName_ = nullptr;

void VulkanDebug::initialize(VkInstance instance) {
    setObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
}

void VulkanDebug::setObjectName(VkDevice device, uint64_t object, VkObjectType type, const char* name) {
    if (!setObjectName_ || !device || !name) return;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = type;
    nameInfo.objectHandle = object;
    nameInfo.pObjectName = name;

    setObjectName_(device, &nameInfo);
}

void VulkanDebug::setImageName(VkDevice device, VkImage image, const char* name) {
    setObjectName(device, reinterpret_cast<uint64_t>(image), VK_OBJECT_TYPE_IMAGE, name);
}

void VulkanDebug::setMemoryName(VkDevice device, VkDeviceMemory memory, const char* name) {
    setObjectName(device, reinterpret_cast<uint64_t>(memory), VK_OBJECT_TYPE_DEVICE_MEMORY, name);
}

void VulkanDebug::setImageViewName(VkDevice device, VkImageView view, const char* name) {
    setObjectName(device, reinterpret_cast<uint64_t>(view), VK_OBJECT_TYPE_IMAGE_VIEW, name);
}

void VulkanDebug::setSamplerName(VkDevice device, VkSampler sampler, const char* name) {
    setObjectName(device, reinterpret_cast<uint64_t>(sampler), VK_OBJECT_TYPE_SAMPLER, name);
}

void VulkanDebug::setBufferName(VkDevice device, VkBuffer buffer, const char* name) {
    setObjectName(device, reinterpret_cast<uint64_t>(buffer), VK_OBJECT_TYPE_BUFFER, name);
}
