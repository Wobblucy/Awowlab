#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

struct GLFWwindow;

class VulkanInstance {
public:
    VulkanInstance();
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    bool initialize(GLFWwindow* window, bool enableValidationLayers = true);
    void cleanup();

    VkInstance getInstance() const { return instance; }
    VkSurfaceKHR getSurface() const { return surface; }
    bool validationEnabled() const { return enableValidationLayers; }

private:
    VkInstance instance;
    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT debugMessenger;
    bool enableValidationLayers;

    bool createInstance();
    bool createSurface(GLFWwindow* window);
    bool setupDebugMessenger();

    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};
