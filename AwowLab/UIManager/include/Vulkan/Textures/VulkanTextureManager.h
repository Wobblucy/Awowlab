#pragma once
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

struct TextureHandle {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t width = 0;   // Actual texture width in pixels
    uint32_t height = 0;  // Actual texture height in pixels

    bool isValid() const { return image != VK_NULL_HANDLE; }
};

// Manages Vulkan texture creation and destruction
class VulkanTextureManager {
public:
    VulkanTextureManager() = default;
    ~VulkanTextureManager();

    VulkanTextureManager(const VulkanTextureManager&) = delete;
    VulkanTextureManager& operator=(const VulkanTextureManager&) = delete;

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkQueue graphicsQueue, VkCommandPool commandPool,
                   VkDescriptorPool descriptorPool);

    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

    // Create texture from raw RGBA pixel data
    // debugName is optional - used for Vulkan debug naming when validation layers enabled
    std::optional<TextureHandle> createTexture(const unsigned char* pixels,
                                                int width, int height,
                                                const char* debugName = nullptr);

    // Create a solid color texture (for default/placeholder icons)
    std::optional<TextureHandle> createSolidColorTexture(int width, int height,
                                                          uint8_t r, uint8_t g,
                                                          uint8_t b, uint8_t a = 255,
                                                          const char* debugName = nullptr);

    // Load texture from PNG file
    std::optional<TextureHandle> loadTextureFromFile(const std::string& filepath,
                                                      const char* debugName = nullptr);

    // Queue a texture for deferred destruction (safe during rendering)
    // skipImGui: set to true when ImGui may already be shut down (during app
    // cleanup). The ImGui descriptor set is still freed if the ImGui Vulkan
    // backend turns out to be running, so passing true mid-session is safe.
    void destroyTexture(TextureHandle& texture, bool skipImGui = false);

    // Process the deletion queue - call once per frame
    // Destroys textures that have been queued for FRAMES_TO_WAIT frames
    void processDeletionQueue();

    // Flush all pending deletions immediately (waits for GPU idle)
    // Call during cleanup before shutting down Vulkan
    void flushDeletionQueue(bool skipImGui = false);

    static ImTextureID getImGuiTextureID(const TextureHandle& texture) {
        return (ImTextureID)texture.descriptorSet;
    }

private:
    bool uploadPixels(VkImage image, const unsigned char* pixels, uint32_t width, uint32_t height);

    void destroyTextureImmediate(TextureHandle& texture, bool skipImGui);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    static constexpr uint32_t FRAMES_TO_WAIT = 3;
    struct PendingDeletion {
        TextureHandle texture;
        uint32_t frameQueued;
        bool skipImGui;
    };
    std::vector<PendingDeletion> deletionQueue_;
    mutable std::mutex deletionQueueMutex_;
    std::atomic<uint32_t> currentFrame_{0};
};
