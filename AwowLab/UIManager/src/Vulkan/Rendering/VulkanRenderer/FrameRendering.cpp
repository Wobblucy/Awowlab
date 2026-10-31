#include "Vulkan/Rendering/VulkanRenderer.h"
#include "UI/Panels/Navigation/UIEncounterSelector.h"
#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/Panels/WarcraftLogs/UIWarcraftLogsPanel.h"
#include "Core/DebugConfig.h"
#include "Core/ErrorLogger.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <array>
#include <cmath>

bool VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    if (imguiManager->getColorToggle()) {
        clearValues[0].color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    } else {
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    }
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    imguiManager->render3D(vulkanDevice->getLogicalDevice(), commandBuffer, vulkanSwapchain->getExtent());

    imguiManager->render(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VulkanRenderer::handleDeviceLost(const char* where) {
    if (deviceLost) {
        return;
    }
    deviceLost = true;

    AWOW_LOG_ERROR("Vulkan", std::string("Device lost in ") + where + ", closing");
#ifdef _WIN32
    MessageBoxA(nullptr, L("error.gpu_lost_message"), L("error.gpu_lost_title"), MB_ICONERROR);
#endif
    if (window) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

VulkanRenderer::FrameAcquireResult VulkanRenderer::beginFrame() {
    if (deviceLost) {
        return {false, 0, VK_NULL_HANDLE};
    }

    VkDevice device = vulkanDevice->getLogicalDevice();
    size_t frame = currentFrame;

    VkFence inFlightFence = vulkanSync->getInFlightFence(frame);
    if (inFlightFence == VK_NULL_HANDLE) {
        AWOW_LOG_ERROR("Vulkan", "In-flight fence is null for frame " + std::to_string(frame));
        return {false, 0, VK_NULL_HANDLE};
    }
    VkResult waitResult = vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    if (waitResult == VK_ERROR_DEVICE_LOST) {
        handleDeviceLost("frame fence wait");
        return {false, 0, VK_NULL_HANDLE};
    } else if (waitResult != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Waiting for the frame fence failed, result=" + std::to_string(static_cast<int>(waitResult)));
        return {false, 0, VK_NULL_HANDLE};
    }

    // This slot's previous work has finished, so deferred texture deletions can run
    if (imguiManager) {
        imguiManager->processTextureCleanup();
    }

    if (vulkanSwapchain->getSwapchain() == VK_NULL_HANDLE) {
        AWOW_LOG_ERROR("Vulkan", "Swapchain is null, attempting recreation");
        if (!recreateSwapchain()) {
            return {false, 0, VK_NULL_HANDLE};
        }
        // Skip this frame after recreation to ensure clean sync state
        return {false, 0, VK_NULL_HANDLE};
    }

    VkSemaphore imageAvailableSemaphore = vulkanSync->getImageAvailableSemaphore(frame);
    if (imageAvailableSemaphore == VK_NULL_HANDLE) {
        AWOW_LOG_ERROR("Vulkan", "Image available semaphore is null for frame " + std::to_string(frame));
        return {false, 0, VK_NULL_HANDLE};
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, vulkanSwapchain->getSwapchain(),
                                           UINT64_MAX,
                                           imageAvailableSemaphore,
                                           VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
        AWOW_DEBUG_LOG(VULKAN_RESOURCES, "Swapchain out of date, recreating");
        recreateSwapchain();
        return {false, 0, VK_NULL_HANDLE};
    } else if (result == VK_ERROR_DEVICE_LOST) {
        handleDeviceLost("image acquire");
        return {false, 0, VK_NULL_HANDLE};
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        AWOW_LOG_ERROR("Vulkan", "Failed to acquire swapchain image, result=" + std::to_string(static_cast<int>(result)));
        return {false, 0, VK_NULL_HANDLE};
    }

    if (imageIndex >= swapchainFramebuffers.size()) {
        AWOW_LOG_ERROR("Vulkan", "imageIndex out of bounds: " + std::to_string(imageIndex) +
                       " >= " + std::to_string(swapchainFramebuffers.size()));
        recreateSwapchain();
        return {false, 0, VK_NULL_HANDLE};
    }

    // The acquired image may still be in use by an earlier frame slot
    VkFence imageInFlightFence = vulkanSync->getImageInFlightFence(imageIndex);
    if (imageInFlightFence != VK_NULL_HANDLE) {
        VkResult imageWaitResult = vkWaitForFences(device, 1, &imageInFlightFence, VK_TRUE, UINT64_MAX);
        if (imageWaitResult == VK_ERROR_DEVICE_LOST) {
            handleDeviceLost("swapchain image fence wait");
            return {false, 0, VK_NULL_HANDLE};
        } else if (imageWaitResult != VK_SUCCESS) {
            AWOW_LOG_ERROR("Vulkan", "Waiting for the swapchain image fence failed, result=" +
                           std::to_string(static_cast<int>(imageWaitResult)));
            return {false, 0, VK_NULL_HANDLE};
        }
    }

    vulkanSync->setImageInFlightFence(imageIndex, inFlightFence);

    // The fence is reset in submitAndPresent right before the submit, so a
    // path that bails out before submitting leaves it signalled

    VkCommandBuffer commandBuffer = vulkanCommands->getCommandBuffer(frame);
    vkResetCommandBuffer(commandBuffer, 0);

    return {true, imageIndex, commandBuffer};
}

bool VulkanRenderer::beginRenderPass(VkCommandBuffer cmd, uint32_t imageIndex, const VkClearColorValue& clearColor) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Failed to begin recording command buffer");
        return false;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    return true;
}

void VulkanRenderer::endRenderPassAndSubmit(VkCommandBuffer cmd, uint32_t imageIndex) {
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Failed to end command buffer - attempting recovery");
        // Submit anyway so the semaphores stay consistent
    }

    submitAndPresent(cmd, imageIndex);
}

void VulkanRenderer::submitMinimalFrame(VkCommandBuffer cmd, uint32_t imageIndex) {
    // Rendering failed after an image was acquired. An empty pass still
    // consumes the acquire semaphore and signals the fence.
    AWOW_DEBUG_LOG(VULKAN_RESOURCES, "Submitting minimal frame to maintain sync");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Frame recovery failed to begin command buffer - sync state corrupted");
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkanSwapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Frame recovery failed to end command buffer - sync state corrupted");
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }

    submitAndPresent(cmd, imageIndex);
}

void VulkanRenderer::submitAndPresent(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {vulkanSync->getImageAvailableSemaphore(currentFrame)};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {vulkanSync->getRenderFinishedSemaphore(currentFrame)};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkDevice device = vulkanDevice->getLogicalDevice();
    VkFence inFlightFence = vulkanSync->getInFlightFence(currentFrame);
    vkResetFences(device, 1, &inFlightFence);

    VkResult submitResult = vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, inFlightFence);
    if (submitResult != VK_SUCCESS) {
        // The fence was reset but nothing will signal it now. Put a signalled
        // one in its place or the next wait on this slot never returns.
        vulkanSync->recreateInFlightFence(device, currentFrame);
        if (submitResult == VK_ERROR_DEVICE_LOST) {
            handleDeviceLost("queue submit");
        } else {
            AWOW_LOG_ERROR("Vulkan", "Failed to submit command buffer, result=" +
                           std::to_string(static_cast<int>(submitResult)));
        }
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {vulkanSwapchain->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(vulkanDevice->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        AWOW_DEBUG_LOG(VULKAN_RESOURCES, "Present triggered swapchain recreate");
        framebufferResized = false;
        recreateSwapchain();
    } else if (result == VK_ERROR_DEVICE_LOST) {
        handleDeviceLost("present");
    } else if (result != VK_SUCCESS) {
        AWOW_LOG_ERROR("Vulkan", "Failed to present, result=" + std::to_string(static_cast<int>(result)));
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::drawFrame() {
    auto frame = beginFrame();
    if (!frame.success) return;

    imguiManager->newFrame();

    // recordCommandBuffer handles full render pass (begin, 3D + ImGui render, end)
    if (!recordCommandBuffer(frame.commandBuffer, frame.imageIndex)) {
        AWOW_LOG_ERROR("Vulkan", "Failed to record command buffer in drawFrame");
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return;
    }

    submitAndPresent(frame.commandBuffer, frame.imageIndex);
}

bool VulkanRenderer::drawFrameWithEncounterSelector(UIEncounterSelector* selector) {
    auto frame = beginFrame();
    if (!frame.success) return false;

    imguiManager->newFrame(false);
    bool selectionMade = selector->render();

    constexpr VkClearColorValue darkGray = {{0.1f, 0.1f, 0.1f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, darkGray)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return false;
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);

    return selectionMade;
}

bool VulkanRenderer::drawFrameWithLandingPage(UILandingPage* landingPage) {
    auto frame = beginFrame();
    if (!frame.success) return false;

    imguiManager->newFrame(false);
    UILandingPage::applyLandingTheme();
    bool selectionMade = landingPage->render();

    constexpr VkClearColorValue landingBg = {{0.06f, 0.06f, 0.10f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, landingBg)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return false;
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);

    return selectionMade;
}

bool VulkanRenderer::drawFrameWithFightSelector(UIWarcraftLogsPanel* fightSelector) {
    auto frame = beginFrame();
    if (!frame.success) return false;

    imguiManager->newFrame(false);
    UILandingPage::applyLandingTheme();
    bool selectionMade = fightSelector->render();

    constexpr VkClearColorValue landingBg = {{0.06f, 0.06f, 0.10f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, landingBg)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return false;
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);

    return selectionMade;
}

void VulkanRenderer::drawFrameWithProgress(const std::string& title, const std::string& message, float progress) {
    auto frame = beginFrame();
    if (!frame.success) return;

    imguiManager->newFrame(false);
    UILandingPage::applyLandingTheme();

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 150), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.15f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin(title.c_str(), nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", message.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        if (progress >= 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
            ImGui::ProgressBar(progress, ImVec2(-1, 24.0f));
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            char percentText[32];
            snprintf(percentText, sizeof(percentText), "%d%%", static_cast<int>(progress * 100));
            float textWidth = ImGui::CalcTextSize(percentText).x;
            ImGui::SetCursorPosX((450.0f - textWidth) * 0.5f - 24.0f);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", percentText);
        } else {
            static float animTime = 0.0f;
            animTime += ImGui::GetIO().DeltaTime;
            int dots = static_cast<int>(animTime * 2.0f) % 4;
            std::string dotsStr(dots + 1, '.');

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Please wait%s", dotsStr.c_str());

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.35f, 0.55f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.20f, 1.0f));
            float oscillate = 0.3f + 0.2f * std::sin(animTime * 3.0f);
            ImGui::ProgressBar(oscillate, ImVec2(-1, 24.0f), "");
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    constexpr VkClearColorValue landingBg = {{0.06f, 0.06f, 0.10f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, landingBg)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return;
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);
}

void VulkanRenderer::drawFrameWithImGuiCallback(std::function<void()> renderCallback) {
    auto frame = beginFrame();
    if (!frame.success) return;

    imguiManager->newFrame(false);
    UILandingPage::applyLandingTheme();
    renderCallback();

    constexpr VkClearColorValue landingBg = {{0.06f, 0.06f, 0.10f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, landingBg)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return;
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);
}

void VulkanRenderer::drawFrameWith3DAndImGui(
    std::function<void(VkCommandBuffer, VkExtent2D)> render3DCallback,
    std::function<void()> imguiCallback) {

    auto frame = beginFrame();
    if (!frame.success) return;

    imguiManager->newFrame(false);
    imguiCallback();

    constexpr VkClearColorValue darkBg = {{0.06f, 0.06f, 0.10f, 1.0f}};
    if (!beginRenderPass(frame.commandBuffer, frame.imageIndex, darkBg)) {
        submitMinimalFrame(frame.commandBuffer, frame.imageIndex);
        return;
    }

    if (render3DCallback) {
        render3DCallback(frame.commandBuffer, vulkanSwapchain->getExtent());
    }

    imguiManager->render(frame.commandBuffer);
    endRenderPassAndSubmit(frame.commandBuffer, frame.imageIndex);
}
