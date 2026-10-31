#include "Vulkan/Textures/VulkanTextureManager.h"
#include "Vulkan/Core/VulkanDebug.h"
#include "Vulkan/Buffers/VulkanBufferUtils.h"
#include <imgui_impl_vulkan.h>
#include <vector>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

VulkanTextureManager::~VulkanTextureManager() {
    // Owners destroy their textures before the manager goes away
    device_ = VK_NULL_HANDLE;
}

bool VulkanTextureManager::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkQueue graphicsQueue, VkCommandPool commandPool,
                                       VkDescriptorPool descriptorPool) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    graphicsQueue_ = graphicsQueue;
    commandPool_ = commandPool;
    descriptorPool_ = descriptorPool;
    return true;
}

namespace {

// Keeps the byte count of a texture well inside 32 bits
constexpr int MAX_TEXTURE_DIMENSION = 16384;
constexpr VkDeviceSize RGBA_BYTES_PER_PIXEL = 4;

// Releases the staging buffer and one-shot command buffer however the upload exits
struct UploadResources {
    VkDevice device;
    VkCommandPool commandPool;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    ~UploadResources() {
        if (commandBuffer != VK_NULL_HANDLE) vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vulkan_buffers::destroyBuffer(device, stagingBuffer, stagingMemory);
    }
};

} // namespace

std::optional<TextureHandle> VulkanTextureManager::createTexture(
    const unsigned char* pixels, int width, int height, const char* debugName) {

    if (!device_ || !pixels || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    if (width > MAX_TEXTURE_DIMENSION || height > MAX_TEXTURE_DIMENSION) {
        return std::nullopt;
    }

    TextureHandle texture;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
        return std::nullopt;
    }

    // From here on destroyTextureImmediate releases whatever has been created so far
    auto fail = [&]() -> std::optional<TextureHandle> {
        destroyTextureImmediate(texture, true);
        return std::nullopt;
    };

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, texture.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = vulkan_buffers::findMemoryType(physicalDevice_, memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).value_or(0);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &texture.memory) != VK_SUCCESS) {
        return fail();
    }

    vkBindImageMemory(device_, texture.image, texture.memory, 0);

    if (!uploadPixels(texture.image, pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return fail();
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS) {
        return fail();
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
        return fail();
    }

    texture.descriptorSet = ImGui_ImplVulkan_AddTexture(texture.sampler,
                                                         texture.imageView,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    texture.width = static_cast<uint32_t>(width);
    texture.height = static_cast<uint32_t>(height);

    if (debugName && VulkanDebug::isEnabled()) {
        VulkanDebug::setImageName(device_, texture.image, debugName);
        std::string memoryName = std::string(debugName) + "_Memory";
        VulkanDebug::setMemoryName(device_, texture.memory, memoryName.c_str());
        std::string viewName = std::string(debugName) + "_View";
        VulkanDebug::setImageViewName(device_, texture.imageView, viewName.c_str());
        std::string samplerName = std::string(debugName) + "_Sampler";
        VulkanDebug::setSamplerName(device_, texture.sampler, samplerName.c_str());
    }

    return texture;
}

// Copies RGBA pixels into the image through a staging buffer and leaves it ready for sampling.
// Blocks until the GPU has finished the copy.
bool VulkanTextureManager::uploadPixels(VkImage image, const unsigned char* pixels,
                                        uint32_t width, uint32_t height) {
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * RGBA_BYTES_PER_PIXEL;
    UploadResources upload{device_, commandPool_};

    if (!vulkan_buffers::createFilledBuffer(device_, physicalDevice_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            pixels, imageSize,
                                            upload.stagingBuffer, upload.stagingMemory)) {
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &upload.commandBuffer) != VK_SUCCESS) {
        upload.commandBuffer = VK_NULL_HANDLE;
        return false;
    }
    VkCommandBuffer commandBuffer = upload.commandBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, upload.stagingBuffer, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        return false;
    }

    return vkQueueWaitIdle(graphicsQueue_) == VK_SUCCESS;
}

std::optional<TextureHandle> VulkanTextureManager::createSolidColorTexture(
    int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    const char* debugName) {

    std::vector<unsigned char> pixels(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }

    return createTexture(pixels.data(), width, height, debugName);
}
std::optional<TextureHandle> VulkanTextureManager::loadTextureFromFile(
    const std::string& filepath, const char* debugName) {

    int width, height, channels;
    unsigned char* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        return std::nullopt;
    }

    auto result = createTexture(pixels, width, height, debugName);
    stbi_image_free(pixels);

    return result;
}

void VulkanTextureManager::destroyTexture(TextureHandle& texture, bool skipImGui) {
    if (!device_ || !texture.isValid()) return;

    // Deferred, because frames still in flight may be sampling it
    PendingDeletion pending;
    pending.texture = texture;
    pending.frameQueued = currentFrame_.load();
    pending.skipImGui = skipImGui;
    {
        std::lock_guard<std::mutex> lock(deletionQueueMutex_);
        deletionQueue_.push_back(pending);
    }

    texture.image = VK_NULL_HANDLE;
    texture.memory = VK_NULL_HANDLE;
    texture.imageView = VK_NULL_HANDLE;
    texture.sampler = VK_NULL_HANDLE;
    texture.descriptorSet = VK_NULL_HANDLE;
}

void VulkanTextureManager::destroyTextureImmediate(TextureHandle& texture, bool /*skipImGui*/) {
    if (!device_) return;

    if (texture.descriptorSet != VK_NULL_HANDLE) {
        // The set came from ImGui's descriptor pool. skipImGui is passed by
        // cleanup paths that also run mid-session (leaving the landing page
        // or Play Along), and honouring it there leaked sets from the pool.
        // So free the set whenever the ImGui Vulkan backend is still
        // running; skipImGui only matters once it has shut down, and then
        // the pool is gone and there is nothing to free.
        const bool imguiBackendAlive = ImGui::GetCurrentContext() != nullptr &&
                                       ImGui::GetIO().BackendRendererUserData != nullptr;
        if (imguiBackendAlive) {
            ImGui_ImplVulkan_RemoveTexture(texture.descriptorSet);
        }
        texture.descriptorSet = VK_NULL_HANDLE;
    }
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device_, texture.sampler, nullptr);
        texture.sampler = VK_NULL_HANDLE;
    }
    if (texture.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, texture.imageView, nullptr);
        texture.imageView = VK_NULL_HANDLE;
    }
    if (texture.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, texture.image, nullptr);
        texture.image = VK_NULL_HANDLE;
    }
    if (texture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, texture.memory, nullptr);
        texture.memory = VK_NULL_HANDLE;
    }
}

void VulkanTextureManager::processDeletionQueue() {
    if (!device_) {
        currentFrame_++;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(deletionQueueMutex_);
        if (deletionQueue_.empty()) {
            currentFrame_++;
            return;
        }

        auto it = deletionQueue_.begin();
        uint32_t frame = currentFrame_.load();
        while (it != deletionQueue_.end()) {
            if (frame - it->frameQueued >= FRAMES_TO_WAIT) {
                destroyTextureImmediate(it->texture, it->skipImGui);
                it = deletionQueue_.erase(it);
            } else {
                ++it;
            }
        }
    }

    currentFrame_++;
}

void VulkanTextureManager::flushDeletionQueue(bool skipImGui) {
    if (!device_) return;

    vkDeviceWaitIdle(device_);

    {
        std::lock_guard<std::mutex> lock(deletionQueueMutex_);
        for (auto& pending : deletionQueue_) {
            destroyTextureImmediate(pending.texture, pending.skipImGui || skipImGui);
        }
        deletionQueue_.clear();
    }
}
