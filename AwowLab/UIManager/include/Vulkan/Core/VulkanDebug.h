#pragma once
#include <vulkan/vulkan.h>

// Debug naming utility for Vulkan objects
// All methods are no-ops when validation layers are disabled (release builds)
// The function pointer is only available when VK_EXT_debug_utils is enabled
class VulkanDebug {
public:
    // Load function pointer from instance (only succeeds if debug_utils enabled)
    static void initialize(VkInstance instance);

    // All these are no-ops if initialize() failed or wasn't called (release builds)
    static void setObjectName(VkDevice device, uint64_t object, VkObjectType type, const char* name);
    static void setImageName(VkDevice device, VkImage image, const char* name);
    static void setMemoryName(VkDevice device, VkDeviceMemory memory, const char* name);
    static void setImageViewName(VkDevice device, VkImageView view, const char* name);
    static void setSamplerName(VkDevice device, VkSampler sampler, const char* name);
    static void setBufferName(VkDevice device, VkBuffer buffer, const char* name);

    static bool isEnabled() { return setObjectName_ != nullptr; }

private:
    static PFN_vkSetDebugUtilsObjectNameEXT setObjectName_;  // null in release
};
