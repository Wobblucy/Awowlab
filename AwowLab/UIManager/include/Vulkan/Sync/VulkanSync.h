#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class VulkanSync {
public:
    VulkanSync();
    ~VulkanSync();

    VulkanSync(const VulkanSync&) = delete;
    VulkanSync& operator=(const VulkanSync&) = delete;

    bool initialize(VkDevice device, size_t maxFramesInFlight);
    void cleanup(VkDevice device);

    // Initialize imagesInFlight tracking for swapchain images
    void initializeImageTracking(size_t swapchainImageCount);

    // Reset sync objects for clean state when starting a new session
    void resetForNewSession(VkDevice device);

    // Track which fence is using a swapchain image
    VkFence getImageInFlightFence(size_t imageIndex) const;
    void setImageInFlightFence(size_t imageIndex, VkFence fence);

    // Replace a frame's fence with a new one in the signalled state. Used
    // when a submit fails after the fence was reset, so the next wait on
    // that frame slot doesn't block forever.
    bool recreateInFlightFence(VkDevice device, size_t index);

    const std::vector<VkSemaphore>& getImageAvailableSemaphores() const { return imageAvailableSemaphores; }
    const std::vector<VkSemaphore>& getRenderFinishedSemaphores() const { return renderFinishedSemaphores; }
    const std::vector<VkFence>& getInFlightFences() const { return inFlightFences; }

    VkSemaphore getImageAvailableSemaphore(size_t index) const { return imageAvailableSemaphores[index]; }
    VkSemaphore getRenderFinishedSemaphore(size_t index) const { return renderFinishedSemaphores[index]; }
    VkFence getInFlightFence(size_t index) const { return inFlightFences[index]; }

private:
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;  // Track which fence is using each swapchain image

    bool createSyncObjects(VkDevice device, size_t maxFramesInFlight);
};
