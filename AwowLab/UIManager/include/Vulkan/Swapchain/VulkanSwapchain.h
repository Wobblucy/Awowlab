#pragma once
#include <vulkan/vulkan.h>
#include <vector>

struct GLFWwindow;

class VulkanSwapchain {
public:
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    VulkanSwapchain();
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    bool initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                   VkSurfaceKHR surface, GLFWwindow* window);
    void cleanup(VkDevice device);
    bool recreate(VkPhysicalDevice physicalDevice, VkDevice device,
                  VkSurfaceKHR surface, GLFWwindow* window);

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkFormat getImageFormat() const { return swapchainImageFormat; }
    VkExtent2D getExtent() const { return swapchainExtent; }
    const std::vector<VkImage>& getImages() const { return swapchainImages; }
    const std::vector<VkImageView>& getImageViews() const { return swapchainImageViews; }
    size_t getImageCount() const { return swapchainImages.size(); }
    uint32_t getMinImageCount() const { return minImageCount; }

    static SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    uint32_t minImageCount;

    bool createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
                        VkSurfaceKHR surface, GLFWwindow* window);
    bool createImageViews(VkDevice device);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window);
};
