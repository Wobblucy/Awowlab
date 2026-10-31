#pragma once
#include <vulkan/vulkan.h>
#include <optional>
#include <string>
#include <vector>

class VulkanDevice {
public:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    VulkanDevice();
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    bool initialize(VkInstance instance, VkSurfaceKHR surface);
    void cleanup();

    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getLogicalDevice() const { return device; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    const QueueFamilyIndices& getQueueFamilies() const { return queueFamilyIndices; }

private:
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    QueueFamilyIndices queueFamilyIndices;

    bool pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    bool createLogicalDevice();

    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool isDeviceSuitableWithReason(VkPhysicalDevice device, VkSurfaceKHR surface, std::string& rejectReason);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    static const std::vector<const char*> deviceExtensions;
};
