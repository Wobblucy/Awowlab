#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include "../../Core/LRUCache.h"
#include "Vulkan/Textures/VulkanTextureManager.h"

// World bounds data from master_map_alignment_coordinates.json
struct MapWorldBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    uint32_t imageWidth = 1024;   // Texture width in pixels
    uint32_t imageHeight = 768;   // Texture height in pixels
    bool valid = false;
};

// Manages world map textures for floor rendering
// Loads PNG files from PNG/Worldmaps/{mapId}.png
// Loads world bounds from master_map_alignment_coordinates.json
class WorldMapTextureManager {
public:
    WorldMapTextureManager();
    ~WorldMapTextureManager();

    WorldMapTextureManager(const WorldMapTextureManager&) = delete;
    WorldMapTextureManager& operator=(const WorldMapTextureManager&) = delete;

    // Initialize with Vulkan context
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkQueue graphicsQueue, VkCommandPool commandPool,
                   VkDescriptorPool descriptorPool);

    // Cleanup resources
    void cleanup();

    // Get texture for a map (loads on-demand if not cached)
    // Returns nullptr if texture couldn't be loaded
    const TextureHandle* getMapTexture(uint32_t mapId, const std::string& mapName);

    // Preload a map texture into the cache (for proactive loading)
    // Returns true if texture was loaded or already cached
    bool preloadMapTexture(uint32_t mapId);

    // Check if a map texture is already loaded in cache
    bool isMapTextureLoaded(uint32_t mapId) const;

    // Get default gray placeholder texture
    const TextureHandle* getDefaultTexture();

    // Check if initialized
    bool isInitialized() const { return textureManager_ && textureManager_->isInitialized(); }

    // Get VkImageView for shader binding (for the current map)
    VkImageView getCurrentImageView() const { return currentTexture_ ? currentTexture_->imageView : VK_NULL_HANDLE; }
    VkSampler getCurrentSampler() const { return currentTexture_ ? currentTexture_->sampler : VK_NULL_HANDLE; }

    // Get ImGui texture ID for the current map (for 2D rendering)
    void* getCurrentImGuiTextureID() const {
        return currentTexture_ ? (void*)currentTexture_->descriptorSet : nullptr;
    }

    // Get current texture dimensions (actual loaded PNG dimensions)
    // Returns {width, height} or {1024, 768} as default if no texture loaded
    std::pair<uint32_t, uint32_t> getCurrentTextureDimensions() const {
        if (currentTexture_ && currentTexture_->width > 0 && currentTexture_->height > 0) {
            return {currentTexture_->width, currentTexture_->height};
        }
        return {1024, 768};  // Default dimensions
    }

    // Set the current active map (for rendering)
    // Returns the world bounds for the map (from master JSON)
    MapWorldBounds setCurrentMap(uint32_t mapId);

    // Get world bounds for a map ID (from master JSON)
    MapWorldBounds getMapBounds(uint32_t mapId) const;

    // Get current map bounds
    const MapWorldBounds& getCurrentBounds() const { return currentBounds_; }

    // Process deferred texture deletions - call once per frame
    void processTextureCleanup();

private:
    // Build file path for a map texture
    std::string buildMapTexturePath(uint32_t mapId);

    // Load world bounds from CSV file
    bool loadWorldBoundsFromCsv();

    // Texture manager for Vulkan operations
    std::unique_ptr<VulkanTextureManager> textureManager_;

    // LRU cache of loaded textures (mapId -> texture handle)
    std::unique_ptr<LRUCache<uint32_t, TextureHandle>> textureCache_;

    // Default gray placeholder texture
    TextureHandle defaultTexture_;

    // Currently active texture (for rendering)
    const TextureHandle* currentTexture_ = nullptr;
    uint32_t currentMapId_ = 0;  // Track current map ID to detect eviction

    // Base path for worldmap files
    std::string worldmapBasePath_;

    // Map ID -> world bounds lookup (loaded from JSON)
    std::unordered_map<uint32_t, MapWorldBounds> mapBoundsLookup_;

    // Currently active map bounds
    MapWorldBounds currentBounds_;

    // Max cached textures
    static constexpr size_t MAX_CACHED_TEXTURES = 20;
};
