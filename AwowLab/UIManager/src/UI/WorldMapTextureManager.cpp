#include "UI/WorldMapTextureManager.h"
#include "Core/Utils/ExecutablePath.h"
#include "AssetResolver.h"
#include "Core/ErrorLogger.h"
#include <filesystem>
#include <fstream>
#include <sstream>

WorldMapTextureManager::WorldMapTextureManager()
    : textureManager_(nullptr)
    , textureCache_(nullptr)
    , currentTexture_(nullptr) {
}

WorldMapTextureManager::~WorldMapTextureManager() {
    cleanup();
}

bool WorldMapTextureManager::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkQueue graphicsQueue, VkCommandPool commandPool,
                                         VkDescriptorPool descriptorPool) {
    textureManager_ = std::make_unique<VulkanTextureManager>();
    if (!textureManager_->initialize(device, physicalDevice, graphicsQueue,
                                      commandPool, descriptorPool)) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to initialize world map texture manager");
        return false;
    }

    textureCache_ = std::make_unique<LRUCache<uint32_t, TextureHandle>>(
        MAX_CACHED_TEXTURES,
        [this](const uint32_t& mapId, TextureHandle& texture) {
            // Clear current texture pointer if we're evicting the current map
            if (mapId == currentMapId_) {
                currentTexture_ = nullptr;
            }
            if (textureManager_) {
                textureManager_->destroyTexture(texture);
            }
        });

    worldmapBasePath_ = awow::getExecutableDirectory() + "PNG\\Worldmaps\\";

    // Create a default gray placeholder texture (larger for floor visibility)
    auto defaultOpt = textureManager_->createSolidColorTexture(256, 256, 64, 64, 72, 255,
                                                                "WorldMap_Default");
    if (!defaultOpt.has_value()) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to create default world map texture");
        return false;
    }
    defaultTexture_ = defaultOpt.value();
    currentTexture_ = &defaultTexture_;

    // Load map world bounds from CSV
    // Without bounds the maps still load, they just cannot be placed.
    loadWorldBoundsFromCsv();

    return true;
}

bool WorldMapTextureManager::loadWorldBoundsFromCsv() {
    // Use AssetResolver to check root first (legacy), then defaults/ (new releases)
    std::string csvPath = awow::AssetResolver::resolve(worldmapBasePath_, "map_world_bounds.csv");

    std::ifstream file(csvPath);
    if (!file.is_open()) {
        AWOW_LOG_INFO("TEXTURE", "Could not open map world bounds: " + csvPath);
        return false;
    }

    std::string line;
    // Skip header
    std::getline(file, line);

    int count = 0;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token;

        // A row that does not parse (hand edit, damaged file) is skipped
        // rather than taking the whole map system down with it
        try {
            uint32_t mapId;
            float minX, minY, maxX, maxY;

            // Parse: mapId,minX,minY,maxX,maxY,imageWidth,imageHeight
            if (std::getline(ss, token, ',')) mapId = std::stoul(token);
            else continue;
            if (std::getline(ss, token, ',')) minX = std::stof(token);
            else continue;
            if (std::getline(ss, token, ',')) minY = std::stof(token);
            else continue;
            if (std::getline(ss, token, ',')) maxX = std::stof(token);
            else continue;
            if (std::getline(ss, token, ',')) maxY = std::stof(token);
            else continue;

            MapWorldBounds bounds;
            bounds.minX = minX;
            bounds.minY = minY;
            bounds.maxX = maxX;
            bounds.maxY = maxY;

            // Parse optional imageWidth, imageHeight columns
            if (std::getline(ss, token, ',')) bounds.imageWidth = std::stoul(token);
            if (std::getline(ss, token, ',')) bounds.imageHeight = std::stoul(token);

            bounds.valid = true;

            mapBoundsLookup_[mapId] = bounds;
            count++;
        } catch (const std::exception&) {
            continue;
        }
    }

    if (count == 0) {
        AWOW_LOG_INFO("TEXTURE", "No map world bounds read from " + csvPath);
    }
    return count > 0;
}

void WorldMapTextureManager::cleanup() {
    currentTexture_ = nullptr;
    currentMapId_ = 0;

    // Cleanup all cached textures (LRU cache calls destroyTexture via eviction callback)
    if (textureCache_) {
        textureCache_->clear();
        textureCache_.reset();
    }

    // Cleanup default texture
    if (textureManager_ && defaultTexture_.isValid()) {
        textureManager_->destroyTexture(defaultTexture_);
    }

    // Flush any pending texture deletions before resetting the manager
    if (textureManager_) {
        textureManager_->flushDeletionQueue(true);
    }

    textureManager_.reset();
}

void WorldMapTextureManager::processTextureCleanup() {
    if (textureManager_) {
        textureManager_->processDeletionQueue();
    }
}

std::string WorldMapTextureManager::buildMapTexturePath(uint32_t mapId) {
    // Check user path first, then defaults/
    // Format: PNG/Worldmaps/{mapId}.png or PNG/Worldmaps/defaults/{mapId}.png
    std::string filename = std::to_string(mapId) + ".png";
    return awow::AssetResolver::resolve(worldmapBasePath_, filename);
}

const TextureHandle* WorldMapTextureManager::getMapTexture(uint32_t mapId, const std::string& /*mapName*/) {
    if (!textureManager_ || !textureCache_) {
        return getDefaultTexture();
    }

    // Check if already loaded (updates LRU order)
    TextureHandle* cached = textureCache_->get(mapId);
    if (cached) {
        return cached;
    }

    std::string filepath = buildMapTexturePath(mapId);

    // Check if file exists first
    if (!std::filesystem::exists(filepath)) {
        return getDefaultTexture();
    }

    std::string debugName = "WorldMap_" + std::to_string(mapId);
    auto textureOpt = textureManager_->loadTextureFromFile(filepath, debugName.c_str());

    if (!textureOpt.has_value()) {
        AWOW_LOG_INFO("TEXTURE", "Failed to load map texture: " + filepath);
        return getDefaultTexture();
    }

    textureCache_->put(mapId, std::move(textureOpt.value()));

    return textureCache_->get(mapId);
}

const TextureHandle* WorldMapTextureManager::getDefaultTexture() {
    return &defaultTexture_;
}

bool WorldMapTextureManager::preloadMapTexture(uint32_t mapId) {
    if (!textureManager_ || !textureCache_) {
        return false;
    }

    // Check if already cached (don't log on cache hit for preload)
    if (textureCache_->contains(mapId)) {
        return true;
    }

    // Load the texture (getMapTexture handles file loading and caching)
    const TextureHandle* texture = getMapTexture(mapId, "");
    return texture != nullptr && texture != &defaultTexture_;
}

bool WorldMapTextureManager::isMapTextureLoaded(uint32_t mapId) const {
    if (!textureCache_) {
        return false;
    }
    return textureCache_->contains(mapId);
}

MapWorldBounds WorldMapTextureManager::setCurrentMap(uint32_t mapId) {
    currentMapId_ = mapId;
    currentTexture_ = getMapTexture(mapId, "");
    currentBounds_ = getMapBounds(mapId);

    return currentBounds_;
}

MapWorldBounds WorldMapTextureManager::getMapBounds(uint32_t mapId) const {
    auto it = mapBoundsLookup_.find(mapId);
    if (it != mapBoundsLookup_.end()) {
        return it->second;
    }
    return MapWorldBounds{};  // Invalid bounds
}
