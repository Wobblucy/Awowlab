#include "Vulkan/Sync/VulkanSync.h"
#include "Core/ErrorLogger.h"
#include <algorithm>

VulkanSync::VulkanSync() {
}

VulkanSync::~VulkanSync() {
}

bool VulkanSync::initialize(VkDevice device, size_t maxFramesInFlight) {
    if (!createSyncObjects(device, maxFramesInFlight)) {
        return false;
    }

    return true;
}

bool VulkanSync::createSyncObjects(VkDevice device, size_t maxFramesInFlight) {
    imageAvailableSemaphores.resize(maxFramesInFlight);
    renderFinishedSemaphores.resize(maxFramesInFlight);
    inFlightFences.resize(maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {

            AWOW_LOG_ERROR("VULKAN", "Failed to create synchronization objects for frame " + std::to_string(i));
            return false;
        }
    }

    return true;
}

void VulkanSync::cleanup(VkDevice device) {
    for (size_t i = 0; i < inFlightFences.size(); i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    renderFinishedSemaphores.clear();
    imageAvailableSemaphores.clear();
    inFlightFences.clear();
}

void VulkanSync::initializeImageTracking(size_t swapchainImageCount) {
    imagesInFlight.resize(swapchainImageCount, VK_NULL_HANDLE);
}

void VulkanSync::resetForNewSession(VkDevice device) {
    // The caller has already waited for the device to go idle, so these
    // fences should be signalled. Waiting on each makes sure the first frame's
    // fence wait can't block.
    for (size_t i = 0; i < inFlightFences.size(); i++) {
        vkWaitForFences(device, 1, &inFlightFences[i], VK_TRUE, UINT64_MAX);
    }

    std::fill(imagesInFlight.begin(), imagesInFlight.end(), VK_NULL_HANDLE);

}

bool VulkanSync::recreateInFlightFence(VkDevice device, size_t index) {
    if (index >= inFlightFences.size()) {
        return false;
    }

    VkFence oldFence = inFlightFences[index];

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence newFence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fenceInfo, nullptr, &newFence) != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "Failed to recreate in-flight fence for frame " + std::to_string(index));
        return false;
    }

    // Swapchain images still pointing at the old fence have nothing pending
    // on it (the submit that would have used it failed)
    std::replace(imagesInFlight.begin(), imagesInFlight.end(), oldFence, VkFence{VK_NULL_HANDLE});

    vkDestroyFence(device, oldFence, nullptr);
    inFlightFences[index] = newFence;
    return true;
}

VkFence VulkanSync::getImageInFlightFence(size_t imageIndex) const {
    if (imageIndex < imagesInFlight.size()) {
        return imagesInFlight[imageIndex];
    }
    return VK_NULL_HANDLE;
}

void VulkanSync::setImageInFlightFence(size_t imageIndex, VkFence fence) {
    if (imageIndex < imagesInFlight.size()) {
        imagesInFlight[imageIndex] = fence;
    }
}
