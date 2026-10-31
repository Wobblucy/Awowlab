#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include "UI/WorldMapTextureManager.h"  // For MapWorldBounds
#include "../../Core/LRUCache.h"
#include "Vulkan/Textures/VulkanTextureManager.h"

// Encounter texture info parsed from filename
// Format: encounter_{encounterId}_x{centerX}_y{centerY}_s{scale}.png
struct EncounterTextureInfo {
    uint32_t encounterId = 0;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float scale = 0.0f;          // Blender orthographic scale (vertical coverage = scale * 2)
    std::string filePath;
    bool valid = false;
};

// Manages encounter-specific floor textures
// Loads PNG files from PNG/EncounterMaps/encounter_{id}_x{x}_y{y}_s{scale}.png
// Calculates world bounds from filename parameters and texture dimensions
class EncounterTextureManager {
public:
    EncounterTextureManager();
    ~EncounterTextureManager();

    EncounterTextureManager(const EncounterTextureManager&) = delete;
    EncounterTextureManager& operator=(const EncounterTextureManager&) = delete;

    // Initialize with Vulkan context
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkQueue graphicsQueue, VkCommandPool commandPool,
                   VkDescriptorPool descriptorPool);

    // Cleanup resources
    void cleanup();

    // Check if initialized
    bool isInitialized() const { return textureManager_ && textureManager_->isInitialized(); }

    // Set the current encounter (loads texture if available)
    // Returns valid bounds if encounter texture found, invalid bounds otherwise
    // Caller should fall back to mapId-based loading if bounds are invalid
    MapWorldBounds setCurrentEncounter(uint32_t encounterId);

    // Check if an encounter texture exists for the given encounter ID
    bool hasEncounterTexture(uint32_t encounterId);

    // Get VkImageView for shader binding (for the current encounter)
    VkImageView getCurrentImageView() const { return currentTexture_ ? currentTexture_->imageView : VK_NULL_HANDLE; }
    VkSampler getCurrentSampler() const { return currentTexture_ ? currentTexture_->sampler : VK_NULL_HANDLE; }

    // Get ImGui texture ID for the current encounter (for 2D rendering)
    void* getCurrentImGuiTextureID() const {
        return currentTexture_ ? (void*)currentTexture_->descriptorSet : nullptr;
    }

    // Get current texture dimensions
    std::pair<uint32_t, uint32_t> getCurrentTextureDimensions() const {
        if (currentTexture_ && currentTexture_->width > 0 && currentTexture_->height > 0) {
            return {currentTexture_->width, currentTexture_->height};
        }
        return {1024, 1024};  // Default square dimensions
    }

    // Get current bounds
    const MapWorldBounds& getCurrentBounds() const { return currentBounds_; }

    // Clear the encounter texture info cache (call when PNG/EncounterMaps/ changes)
    void clearInfoCache() { encounterTextureCache_.clear(); }

    // Process deferred texture deletions - call once per frame
    void processTextureCleanup();

private:
    // Find and parse encounter texture file for given encounter ID
    // Scans user directory first, then defaults/
    std::optional<EncounterTextureInfo> findEncounterTexture(uint32_t encounterId);

    // Helper to scan a single directory for encounter texture
    std::optional<EncounterTextureInfo> scanDirectoryForEncounter(
        const std::string& directory, uint32_t encounterId);

    // Parse encounter texture filename to extract bounds info
    // Format: encounter_{encounterId}_x{centerX}_y{centerY}_s{scale}.png
    static std::optional<EncounterTextureInfo> parseEncounterFilename(const std::string& filename);

    // Calculate world bounds from encounter texture info and actual texture dimensions
    MapWorldBounds calculateEncounterBounds(const EncounterTextureInfo& info,
                                            uint32_t textureWidth, uint32_t textureHeight) const;

    // Texture manager for Vulkan operations
    std::unique_ptr<VulkanTextureManager> textureManager_;

    // LRU cache of loaded textures (encounterId -> texture handle)
    std::unique_ptr<LRUCache<uint32_t, TextureHandle>> textureCache_;

    // Currently active texture (for rendering)
    const TextureHandle* currentTexture_ = nullptr;

    // Base path for encounter map files
    std::string encounterMapBasePath_;

    // Encounter ID -> texture info cache (avoids re-scanning directory)
    std::unordered_map<uint32_t, EncounterTextureInfo> encounterTextureCache_;

    // Currently active bounds
    MapWorldBounds currentBounds_;

    // Max cached textures (encounter textures can be large, keep few)
    static constexpr size_t MAX_CACHED_TEXTURES = 6;

    // Flag to skip ImGui calls during cleanup (when ImGui may already be shut down)
    bool isCleaningUp_ = false;
};
