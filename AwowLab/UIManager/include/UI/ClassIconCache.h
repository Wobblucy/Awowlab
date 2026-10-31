#pragma once

#include <vulkan/vulkan.h>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <memory>

class VulkanTextureManager;
struct TextureHandle;

/**
 * Lightweight cache for class icons (13 total).
 * Used by UICachedFightsPanel to display player class composition
 * before any encounter is loaded.
 */
class ClassIconCache {
public:
    ClassIconCache();
    ~ClassIconCache();

    // Non-copyable
    ClassIconCache(const ClassIconCache&) = delete;
    ClassIconCache& operator=(const ClassIconCache&) = delete;

    /**
     * Initialize with Vulkan resources.
     * @param iconBasePath Path to PNG/Spell Icons folder
     */
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkQueue graphicsQueue, VkCommandPool commandPool,
                    const std::string& iconBasePath);

    /**
     * Get class icon by WCL class name.
     * @param className WCL class name (e.g., "Mage", "Warrior", "DeathKnight")
     * @return ImTextureID for ImGui rendering, or nullptr if not found
     */
    ImTextureID getClassIcon(const std::string& className);

    /**
     * Clean up Vulkan resources.
     */
    void cleanup();

private:
    void loadIcons(const std::string& basePath);

    std::unique_ptr<VulkanTextureManager> textureManager_;
    std::unordered_map<std::string, TextureHandle> iconCache_;
    VkDevice device_ = VK_NULL_HANDLE;
    bool initialized_ = false;
};
