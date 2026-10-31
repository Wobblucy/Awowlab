#include "Vulkan/Device/VulkanDevice.h"
#include "Vulkan/Swapchain/VulkanSwapchain.h"
#include "Core/ErrorLogger.h"
#include <set>
#include <vector>

const std::vector<const char*> VulkanDevice::deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VulkanDevice::VulkanDevice()
    : physicalDevice(VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , graphicsQueue(VK_NULL_HANDLE)
    , presentQueue(VK_NULL_HANDLE)
{
}

VulkanDevice::~VulkanDevice() {
    cleanup();
}

bool VulkanDevice::initialize(VkInstance instance, VkSurfaceKHR surface) {
    if (!pickPhysicalDevice(instance, surface)) {
        return false;
    }

    if (!createLogicalDevice()) {
        return false;
    }

    return true;
}

bool VulkanDevice::pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        AWOW_LOG_ERROR("VULKAN", "No GPU with Vulkan support found - update graphics drivers");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    AWOW_LOG_INFO("VULKAN", "Found " + std::to_string(deviceCount) + " GPU(s):");

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (const auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        std::string deviceType;
        int score = 0;

        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                deviceType = "Discrete GPU";
                score = 1000;  // Strongly prefer discrete GPUs
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                deviceType = "Integrated GPU";
                score = 100;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                deviceType = "Virtual GPU";
                score = 50;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                deviceType = "CPU (software)";
                score = 10;
                break;
            default:
                deviceType = "Other";
                score = 1;
                break;
        }

        std::string rejectReason;
        bool suitable = isDeviceSuitableWithReason(dev, surface, rejectReason);

        std::string logMsg = "  - " + std::string(props.deviceName) + " (" + deviceType + ")";
        if (suitable) {
            logMsg += " [SUITABLE, score=" + std::to_string(score) + "]";
            if (score > bestScore) {
                bestScore = score;
                bestDevice = dev;
            }
        } else {
            logMsg += " [REJECTED: " + rejectReason + "]";
        }
        AWOW_LOG_INFO("VULKAN", logMsg);
    }

    if (bestDevice == VK_NULL_HANDLE) {
        AWOW_LOG_ERROR("VULKAN", "No suitable GPU found - requires Vulkan 1.0 with swapchain support");
        return false;
    }

    physicalDevice = bestDevice;
    queueFamilyIndices = findQueueFamilies(physicalDevice, surface);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    AWOW_LOG_INFO("VULKAN", "Selected GPU: " + std::string(deviceProperties.deviceName));

    return true;
}

bool VulkanDevice::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilyIndices.graphicsFamily.value(),
        queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create Vulkan logical device");
        return false;
    }

    vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, queueFamilyIndices.presentFamily.value(), 0, &presentQueue);

    return true;
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    std::string reason;
    return isDeviceSuitableWithReason(dev, surface, reason);
}

bool VulkanDevice::isDeviceSuitableWithReason(VkPhysicalDevice dev, VkSurfaceKHR surface, std::string& rejectReason) {
    QueueFamilyIndices indices = findQueueFamilies(dev, surface);

    if (!indices.graphicsFamily.has_value()) {
        rejectReason = "no graphics queue family";
        return false;
    }

    if (!indices.presentFamily.has_value()) {
        rejectReason = "no present queue family";
        return false;
    }

    if (!checkDeviceExtensionSupport(dev)) {
        rejectReason = "missing VK_KHR_swapchain extension";
        return false;
    }

    auto swapChainSupport = VulkanSwapchain::querySwapChainSupport(dev, surface);
    if (swapChainSupport.formats.empty()) {
        rejectReason = "no supported surface formats";
        return false;
    }

    if (swapChainSupport.presentModes.empty()) {
        rejectReason = "no supported present modes";
        return false;
    }

    // Check push constant size (annotation shaders require 128 bytes - Vulkan minimum guarantee)
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);
    constexpr uint32_t REQUIRED_PUSH_CONSTANT_SIZE = 128;
    if (props.limits.maxPushConstantsSize < REQUIRED_PUSH_CONSTANT_SIZE) {
        rejectReason = "insufficient push constant size (" +
                       std::to_string(props.limits.maxPushConstantsSize) +
                       " bytes, need " + std::to_string(REQUIRED_PUSH_CONSTANT_SIZE) + ")";
        return false;
    }

    rejectReason = "";
    return true;
}

VulkanDevice::QueueFamilyIndices VulkanDevice::findQueueFamilies(
    VkPhysicalDevice dev, VkSurfaceKHR surface) {

    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice dev) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

void VulkanDevice::cleanup() {
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
}
