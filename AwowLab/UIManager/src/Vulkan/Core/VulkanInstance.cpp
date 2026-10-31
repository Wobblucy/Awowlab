#include "Vulkan/Core/VulkanInstance.h"
#include "Vulkan/Core/VulkanDebug.h"
#include "Core/ErrorLogger.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <vector>

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

VulkanInstance::VulkanInstance()
    : instance(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , debugMessenger(VK_NULL_HANDLE)
    , enableValidationLayers(false)
{
}

VulkanInstance::~VulkanInstance() {
    cleanup();
}

bool VulkanInstance::initialize(GLFWwindow* window, bool enableValidation) {
    this->enableValidationLayers = enableValidation;

    if (enableValidationLayers && !checkValidationLayerSupport()) {
        // Expected for most users: the validation layers ship with the Vulkan SDK
        enableValidationLayers = false;
    }

    if (!createInstance()) {
        return false;
    }

    VulkanDebug::initialize(instance);

    if (enableValidationLayers && !setupDebugMessenger()) {
        // Not fatal; carry on without validation output
        enableValidationLayers = false;
    }

    if (!createSurface(window)) {
        return false;
    }

    return true;
}

bool VulkanInstance::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "AwowLab";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();

    std::string extList = "Required extensions (" + std::to_string(extensions.size()) + "): ";
    for (size_t i = 0; i < extensions.size(); i++) {
        if (i > 0) extList += ", ";
        extList += extensions[i];
    }
    AWOW_LOG_INFO("VULKAN", extList);

    if (extensions.empty()) {
        AWOW_LOG_ERROR("VULKAN", "GLFW returned no required extensions - GLFW may not be initialized or Vulkan loader missing");
        return false;
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::string errorMsg = "vkCreateInstance failed with error code: " + std::to_string(static_cast<int>(result));
        switch (result) {
            case VK_ERROR_OUT_OF_HOST_MEMORY: errorMsg += " (VK_ERROR_OUT_OF_HOST_MEMORY)"; break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: errorMsg += " (VK_ERROR_OUT_OF_DEVICE_MEMORY)"; break;
            case VK_ERROR_INITIALIZATION_FAILED: errorMsg += " (VK_ERROR_INITIALIZATION_FAILED)"; break;
            case VK_ERROR_LAYER_NOT_PRESENT: errorMsg += " (VK_ERROR_LAYER_NOT_PRESENT)"; break;
            case VK_ERROR_EXTENSION_NOT_PRESENT: errorMsg += " (VK_ERROR_EXTENSION_NOT_PRESENT)"; break;
            case VK_ERROR_INCOMPATIBLE_DRIVER: errorMsg += " (VK_ERROR_INCOMPATIBLE_DRIVER)"; break;
            default: errorMsg += " (unknown)"; break;
        }
        AWOW_LOG_ERROR("VULKAN", errorMsg);
        return false;
    }

    return true;
}

bool VulkanInstance::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create window surface");
        return false;
    }
    return true;
}

bool VulkanInstance::setupDebugMessenger() {
    if (!enableValidationLayers) return true;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != nullptr) {
        if (func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

std::vector<const char*> VulkanInstance::getRequiredExtensions() {
    if (!glfwVulkanSupported()) {
        AWOW_LOG_ERROR("VULKAN", "GLFW reports Vulkan is NOT supported - vulkan-1.dll may be missing. "
                                 "Try reinstalling GPU drivers or download Vulkan Runtime from https://vulkan.lunarg.com/sdk/home");
        return {};
    }

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
        AWOW_LOG_ERROR("VULKAN", "glfwGetRequiredInstanceExtensions returned NULL - Vulkan loader issue");
        return {};
    }

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanInstance::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT /*messageSeverity*/,
    VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/) {

    // Only reachable with the Vulkan SDK's layers installed, so stderr is enough
    std::cerr << "Validation layer: " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

void VulkanInstance::cleanup() {
    if (debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, nullptr);
            debugMessenger = VK_NULL_HANDLE;
        }
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}
