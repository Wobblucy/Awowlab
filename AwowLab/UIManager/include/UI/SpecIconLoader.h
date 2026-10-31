#pragma once
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <unordered_map>
#include <string>
#include <memory>
#include "../../Core/LRUCache.h"
#include "Vulkan/Textures/VulkanTextureManager.h"

// Manages specialization icon textures for ClassIcon shape rendering
// Uses VulkanTextureManager for texture creation and LRUCache for caching
class SpecIconLoader {
public:
    SpecIconLoader();
    ~SpecIconLoader();

    SpecIconLoader(const SpecIconLoader&) = delete;
    SpecIconLoader& operator=(const SpecIconLoader&) = delete;

    // Initialize with Vulkan context
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkQueue graphicsQueue, VkCommandPool commandPool,
                   VkDescriptorPool descriptorPool);

    // Cleanup resources
    void cleanup();

    // Get texture handle for a spec (loads on-demand if not cached)
    // Returns the texture handle for binding in descriptor sets
    TextureHandle getSpecIcon(uint16_t specId);

    // Get ImTextureID for ImGui rendering (e.g., in UI panels)
    ImTextureID getSpecIconForImGui(uint16_t specId);

    // Get default "unknown spec" icon
    TextureHandle getDefaultIcon();
    ImTextureID getDefaultIconForImGui();

    // Get Vulkan resources for descriptor binding
    VkImageView getSpecIconImageView(uint16_t specId);
    VkSampler getSpecIconSampler(uint16_t specId);

    // Check if a spec has an icon mapping
    bool hasIconMapping(uint16_t specId) const;

    // Get the PNG filename for a spec ID (for debugging)
    std::string getSpecIconFilename(uint16_t specId) const;

    // Process deferred texture deletions - call once per frame
    void processTextureCleanup();

private:
    // Texture manager for Vulkan operations
    std::unique_ptr<VulkanTextureManager> textureManager_;

    // Spec ID -> PNG filename mapping (hardcoded)
    std::unordered_map<uint16_t, std::string> specToFilename_;

    // LRU cache of loaded textures (spec ID -> texture handle)
    std::unique_ptr<LRUCache<uint16_t, TextureHandle>> textureCache_;

    // Default icon for unknown specs
    TextureHandle defaultTexture_;

    // Base path for icon files (set at runtime relative to executable)
    std::string iconBasePath_;

    // Max cached textures (only ~39 specs exist, so 50 is plenty)
    static constexpr size_t MAX_CACHED_TEXTURES = 50;

    // Initialize the hardcoded spec-to-filename mapping
    void initializeSpecMappings();
};
