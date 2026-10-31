#include "Vulkan/Rendering/VulkanRenderer.h"
#include <GLFW/glfw3.h>

void VulkanRenderer::handleResize() {
    framebufferResized = true;
}

void VulkanRenderer::waitForDeviceIdle() {
    if (vulkanDevice && vulkanDevice->getLogicalDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkanDevice->getLogicalDevice());
    }
}

bool VulkanRenderer::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vulkanDevice->getLogicalDevice());

    cleanupSwapchain();

    if (!vulkanSwapchain->initialize(vulkanDevice->getPhysicalDevice(),
                                      vulkanDevice->getLogicalDevice(),
                                      vulkanInstance->getSurface(),
                                      window)) {
        return false;
    }

    vulkanSync->initializeImageTracking(vulkanSwapchain->getImageCount());

    auto extent = vulkanSwapchain->getExtent();
    if (!vulkanDepthBuffer->initialize(vulkanDevice->getPhysicalDevice(),
                                        vulkanDevice->getLogicalDevice(),
                                        extent.width,
                                        extent.height)) {
        return false;
    }

    if (!createFramebuffers()) {
        return false;
    }

    return true;
}

void VulkanRenderer::cleanupSwapchain() {
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(vulkanDevice->getLogicalDevice(), framebuffer, nullptr);
    }
    swapchainFramebuffers.clear();

    if (vulkanDepthBuffer) {
        vulkanDepthBuffer->cleanup(vulkanDevice->getLogicalDevice());
    }

    vulkanSwapchain->cleanup(vulkanDevice->getLogicalDevice());
}

void VulkanRenderer::cleanup() {
    if (vulkanDevice && vulkanDevice->getLogicalDevice()) {
        vkDeviceWaitIdle(vulkanDevice->getLogicalDevice());

        // Reverse creation order: app resources, swapchain, render pass and
        // sync objects, then device and instance
        if (imguiManager) {
            imguiManager->cleanup(vulkanDevice->getLogicalDevice());
        }

        cleanupSwapchain();

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vulkanDevice->getLogicalDevice(), renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        if (vulkanSync) {
            vulkanSync->cleanup(vulkanDevice->getLogicalDevice());
        }

        if (vulkanCommands) {
            vulkanCommands->cleanup(vulkanDevice->getLogicalDevice());
        }

        if (vulkanDevice) {
            vulkanDevice->cleanup();
        }

        if (vulkanInstance) {
            vulkanInstance->cleanup();
        }
    }
}
