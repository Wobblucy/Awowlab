#include "Vulkan/Rendering/VulkanRenderer.h"
#include "Core/ErrorLogger.h"
#include <GLFW/glfw3.h>
#include <array>



VulkanRenderer::VulkanRenderer()
    : window(nullptr)
    , currentFrame(0)
    , framebufferResized(false)
    , renderPass(VK_NULL_HANDLE)
    , vulkanInstance(std::make_unique<VulkanInstance>())
    , vulkanDevice(std::make_unique<VulkanDevice>())
    , vulkanSwapchain(std::make_unique<VulkanSwapchain>())
    , vulkanCommands(std::make_unique<VulkanCommands>())
    , vulkanSync(std::make_unique<VulkanSync>())
    , vulkanDepthBuffer(std::make_unique<VulkanDepthBuffer>())
    , imguiManager(std::make_unique<ImGuiManager>())
{
}

VulkanRenderer::~VulkanRenderer() {
    cleanup();
}

bool VulkanRenderer::initialize(GLFWwindow* win) {
    this->window = win;

    // Only enable validation layers in Debug builds - they require Vulkan SDK installed
#ifdef NDEBUG
    constexpr bool enableValidation = false;
#else
    constexpr bool enableValidation = true;
#endif
    if (!vulkanInstance->initialize(win, enableValidation)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize Vulkan instance - check GPU drivers");
        return false;
    }

    if (!vulkanDevice->initialize(vulkanInstance->getInstance(),
                                   vulkanInstance->getSurface())) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize Vulkan device - no compatible GPU found");
        return false;
    }

    // Wait for valid window size before creating swapchain (e.g., window minimized at startup)
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    if (!vulkanSwapchain->initialize(vulkanDevice->getPhysicalDevice(),
                                      vulkanDevice->getLogicalDevice(),
                                      vulkanInstance->getSurface(),
                                      window)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize Vulkan swapchain");
        return false;
    }

    auto extent = vulkanSwapchain->getExtent();
    if (!vulkanDepthBuffer->initialize(vulkanDevice->getPhysicalDevice(),
                                        vulkanDevice->getLogicalDevice(),
                                        extent.width,
                                        extent.height)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize depth buffer");
        return false;
    }

    if (!createRenderPass()) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create render pass");
        return false;
    }

    if (!createFramebuffers()) {
        AWOW_LOG_ERROR("VULKAN", "Failed to create framebuffers");
        return false;
    }

    auto queueFamilies = vulkanDevice->getQueueFamilies();
    if (!vulkanCommands->initialize(vulkanDevice->getLogicalDevice(),
                                     queueFamilies.graphicsFamily.value(),
                                     MAX_FRAMES_IN_FLIGHT)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize Vulkan command buffers");
        return false;
    }

    if (!vulkanSync->initialize(vulkanDevice->getLogicalDevice(),
                                 MAX_FRAMES_IN_FLIGHT)) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize Vulkan sync objects");
        return false;
    }

    vulkanSync->initializeImageTracking(vulkanSwapchain->getImageCount());

    if (!imguiManager->initialize(window,
                                   vulkanInstance->getInstance(),
                                   vulkanDevice->getPhysicalDevice(),
                                   vulkanDevice->getLogicalDevice(),
                                   queueFamilies.graphicsFamily.value(),
                                   vulkanDevice->getGraphicsQueue(),
                                   renderPass,
                                   vulkanSwapchain->getMinImageCount(),
                                   static_cast<uint32_t>(vulkanSwapchain->getImageCount()))) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize ImGui");
        return false;
    }

    if (!imguiManager->initialize3DRendering(vulkanDevice->getLogicalDevice(),
                                             vulkanDevice->getPhysicalDevice(),
                                             renderPass,
                                             vulkanDevice->getGraphicsQueue(),
                                             vulkanCommands->getCommandPool())) {
        AWOW_LOG_ERROR("VULKAN", "Failed to initialize 3D rendering pipeline");
        return false;
    }

    if (!imguiManager->initializeSpellIcons(vulkanDevice->getLogicalDevice(),
                                            vulkanDevice->getPhysicalDevice(),
                                            vulkanDevice->getGraphicsQueue(),
                                            vulkanCommands->getCommandPool())) {
        AWOW_LOG_WARNING("TEXTURE", "Spell icons not found - icons will show as gray placeholders. Installed copies get them through the updater; source builds can run tools/fetch-assets.py");
        // Don't fail the whole initialization - spell icons are optional
    }

    AWOW_LOG_INFO("VULKAN", "Vulkan renderer initialized successfully");
    return true;
}

bool VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = vulkanSwapchain->getImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = vulkanDepthBuffer->getFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(vulkanDevice->getLogicalDevice(), &renderPassInfo,
                          nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        AWOW_LOG_ERROR("VULKAN", "vkCreateRenderPass failed with VkResult: " + std::to_string(static_cast<int>(result)));
        return false;
    }

    return true;
}

bool VulkanRenderer::createFramebuffers() {
    const auto& swapchainImageViews = vulkanSwapchain->getImageViews();
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            vulkanDepthBuffer->getImageView()
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = vulkanSwapchain->getExtent().width;
        framebufferInfo.height = vulkanSwapchain->getExtent().height;
        framebufferInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(vulkanDevice->getLogicalDevice(), &framebufferInfo,
                               nullptr, &swapchainFramebuffers[i]);
        if (result != VK_SUCCESS) {
            AWOW_LOG_ERROR("VULKAN", "vkCreateFramebuffer[" + std::to_string(i) + "] failed with VkResult: " + std::to_string(static_cast<int>(result)));
            return false;
        }
    }

    return true;
}

VkDevice VulkanRenderer::getDevice() const {
    return vulkanDevice ? vulkanDevice->getLogicalDevice() : VK_NULL_HANDLE;
}

VkPhysicalDevice VulkanRenderer::getPhysicalDevice() const {
    return vulkanDevice ? vulkanDevice->getPhysicalDevice() : VK_NULL_HANDLE;
}

VkQueue VulkanRenderer::getGraphicsQueue() const {
    return vulkanDevice ? vulkanDevice->getGraphicsQueue() : VK_NULL_HANDLE;
}

VkCommandPool VulkanRenderer::getCommandPool() const {
    return vulkanCommands ? vulkanCommands->getCommandPool() : VK_NULL_HANDLE;
}

SpellIconLoader* VulkanRenderer::getSpellIconLoader() {
    return imguiManager ? imguiManager->getSpellIconLoader() : nullptr;
}
