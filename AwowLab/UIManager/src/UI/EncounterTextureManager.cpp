#include "UI/EncounterTextureManager.h"
#include "Core/Utils/ExecutablePath.h"
#include "AssetResolver.h"
#include "Core/ErrorLogger.h"
#include <filesystem>

EncounterTextureManager::EncounterTextureManager()
    : textureManager_(nullptr)
    , textureCache_(nullptr)
    , currentTexture_(nullptr) {
}

EncounterTextureManager::~EncounterTextureManager() {
    cleanup();
}

bool EncounterTextureManager::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                          VkQueue graphicsQueue, VkCommandPool commandPool,
                                          VkDescriptorPool descriptorPool) {
    textureManager_ = std::make_unique<VulkanTextureManager>();
    if (!textureManager_->initialize(device, physicalDevice, graphicsQueue,
                                      commandPool, descriptorPool)) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to initialize encounter map texture manager");
        return false;
    }

    textureCache_ = std::make_unique<LRUCache<uint32_t, TextureHandle>>(
        MAX_CACHED_TEXTURES,
        [this](const uint32_t& /*encounterId*/, TextureHandle& texture) {
            if (textureManager_) {
                // Skip ImGui calls during cleanup (ImGui may already be shut down)
                textureManager_->destroyTexture(texture, isCleaningUp_);
            }
        });

    encounterMapBasePath_ = awow::getExecutableDirectory() + "PNG\\EncounterMaps\\";

    return true;
}

void EncounterTextureManager::cleanup() {
    currentTexture_ = nullptr;

    // Set flag to skip ImGui calls during cleanup (ImGui may already be shut down)
    isCleaningUp_ = true;

    // Cleanup all cached textures (LRU cache calls destroyTexture via eviction callback)
    if (textureCache_) {
        textureCache_->clear();
        textureCache_.reset();
    }

    // Flush any pending texture deletions before resetting the manager
    if (textureManager_) {
        textureManager_->flushDeletionQueue(true);
    }

    textureManager_.reset();
    encounterTextureCache_.clear();
}

void EncounterTextureManager::processTextureCleanup() {
    if (textureManager_) {
        textureManager_->processDeletionQueue();
    }
}

std::optional<EncounterTextureInfo> EncounterTextureManager::parseEncounterFilename(const std::string& filename) {
    // Format: encounter_{encounterId}_x{centerX}_y{centerY}_s{scale}.png
    // Example: encounter_2902_x200_y-4980_s300.png

    EncounterTextureInfo info;

    if (filename.find("encounter_") != 0) {
        return std::nullopt;
    }

    // Remove "encounter_" prefix and ".png" suffix
    std::string params = filename.substr(10);  // Skip "encounter_"
    size_t pngPos = params.rfind(".png");
    if (pngPos == std::string::npos) {
        return std::nullopt;
    }
    params = params.substr(0, pngPos);

    // Parse: {encounterId}_x{centerX}_y{centerY}_s{scale}
    // Split by underscores, but handle negative numbers (e.g., y-4980)
    size_t pos = 0;
    size_t nextUnderscore = params.find('_');
    if (nextUnderscore == std::string::npos) {
        return std::nullopt;
    }

    try {
        info.encounterId = std::stoul(params.substr(0, nextUnderscore));
    } catch (...) {
        return std::nullopt;
    }
    pos = nextUnderscore + 1;

    // Parse x{centerX}
    if (params.substr(pos, 1) != "x") {
        return std::nullopt;
    }
    pos += 1;
    nextUnderscore = params.find("_y", pos);
    if (nextUnderscore == std::string::npos) {
        return std::nullopt;
    }
    try {
        info.centerX = std::stof(params.substr(pos, nextUnderscore - pos));
    } catch (...) {
        return std::nullopt;
    }
    pos = nextUnderscore + 2;  // Skip "_y"

    // Parse {centerY}
    nextUnderscore = params.find("_s", pos);
    if (nextUnderscore == std::string::npos) {
        return std::nullopt;
    }
    try {
        info.centerY = std::stof(params.substr(pos, nextUnderscore - pos));
    } catch (...) {
        return std::nullopt;
    }
    pos = nextUnderscore + 2;  // Skip "_s"

    // Parse {scale}
    try {
        info.scale = std::stof(params.substr(pos));
    } catch (...) {
        return std::nullopt;
    }

    if (info.scale <= 0) {
        return std::nullopt;
    }

    info.valid = true;
    return info;
}

std::optional<EncounterTextureInfo> EncounterTextureManager::scanDirectoryForEncounter(
    const std::string& directory, uint32_t encounterId) {
    if (!std::filesystem::exists(directory)) {
        return std::nullopt;
    }

    std::string prefix = "encounter_" + std::to_string(encounterId) + "_";

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (filename.find(prefix) == 0 && filename.ends_with(".png")) {
            auto infoOpt = parseEncounterFilename(filename);
            if (infoOpt.has_value()) {
                infoOpt->filePath = entry.path().string();
                return infoOpt;
            }
        }
    }

    return std::nullopt;
}

std::optional<EncounterTextureInfo> EncounterTextureManager::findEncounterTexture(uint32_t encounterId) {
    // Check cache first
    auto cacheIt = encounterTextureCache_.find(encounterId);
    if (cacheIt != encounterTextureCache_.end()) {
        if (cacheIt->second.valid) {
            return cacheIt->second;
        }
        return std::nullopt;  // Previously searched, not found
    }

    // Check user directory first (PNG/EncounterMaps/)
    auto result = scanDirectoryForEncounter(encounterMapBasePath_, encounterId);
    if (result.has_value()) {
        encounterTextureCache_[encounterId] = *result;
        return result;
    }

    // Fall back to defaults directory (PNG/EncounterMaps/defaults/)
    std::string defaultsPath = encounterMapBasePath_ + "defaults\\";
    result = scanDirectoryForEncounter(defaultsPath, encounterId);
    if (result.has_value()) {
        encounterTextureCache_[encounterId] = *result;
        return result;
    }

    // Not found - cache negative result
    encounterTextureCache_[encounterId] = EncounterTextureInfo{};
    return std::nullopt;
}

MapWorldBounds EncounterTextureManager::calculateEncounterBounds(
    const EncounterTextureInfo& info,
    uint32_t textureWidth,
    uint32_t textureHeight) const {

    MapWorldBounds bounds;

    // Blender orthographic scale defines vertical coverage (ortho_scale is full size, not half)
    float verticalCoverage = info.scale;

    // Calculate aspect ratio from actual texture dimensions
    float aspectRatio = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);

    float blenderHeight = verticalCoverage;
    float blenderWidth = verticalCoverage * aspectRatio;

    // Transform from Blender coordinates to WoW/AwowLab coordinates:
    // WoW X = -Blender Y
    // WoW Y = Blender X
    float wowCenterX = -info.centerY;
    float wowCenterY = info.centerX;

    // After axis swap: Blender width (X axis) becomes WoW height (Y axis)
    //                  Blender height (Y axis) becomes WoW width (X axis)
    float wowWidth = blenderHeight;
    float wowHeight = blenderWidth;

    bounds.minX = wowCenterX - (wowWidth / 2.0f);
    bounds.maxX = wowCenterX + (wowWidth / 2.0f);
    bounds.minY = wowCenterY - (wowHeight / 2.0f);
    bounds.maxY = wowCenterY + (wowHeight / 2.0f);

    bounds.imageWidth = textureWidth;
    bounds.imageHeight = textureHeight;
    bounds.valid = true;

    return bounds;
}

bool EncounterTextureManager::hasEncounterTexture(uint32_t encounterId) {
    auto infoOpt = findEncounterTexture(encounterId);
    return infoOpt.has_value();
}

MapWorldBounds EncounterTextureManager::setCurrentEncounter(uint32_t encounterId) {
    if (encounterId == 0) {
        return MapWorldBounds{};  // Invalid
    }

    // WarcraftLogs adds a version prefix (e.g., 53180 for Blizzard encounter 3180)
    // Normalize to last 4 digits to match Blizzard encounter IDs
    uint32_t normalizedId = encounterId % 10000;
    encounterId = normalizedId;

    // Clear info cache to re-scan directory (in case textures were added)
    // This is cheap - just clears the filename lookup cache, not loaded textures
    encounterTextureCache_.clear();

    TextureHandle* cached = textureCache_->get(encounterId);
    if (cached) {
        currentTexture_ = cached;
        // Recalculate bounds (in case we need them)
        auto infoOpt = findEncounterTexture(encounterId);
        if (infoOpt.has_value()) {
            currentBounds_ = calculateEncounterBounds(*infoOpt, currentTexture_->width, currentTexture_->height);
        }
        return currentBounds_;
    }

    auto infoOpt = findEncounterTexture(encounterId);
    if (!infoOpt.has_value()) {
        return MapWorldBounds{};  // Caller should fall back to mapId
    }

    const auto& info = *infoOpt;

    if (!std::filesystem::exists(info.filePath)) {
        AWOW_LOG_INFO("TEXTURE", "Encounter texture file missing: " + info.filePath);
        return MapWorldBounds{};
    }

    std::string debugName = "EncounterFloor_" + std::to_string(encounterId);
    auto textureOpt = textureManager_->loadTextureFromFile(info.filePath, debugName.c_str());
    if (!textureOpt.has_value()) {
        AWOW_LOG_INFO("TEXTURE", "Failed to load encounter texture: " + info.filePath);
        return MapWorldBounds{};
    }

    textureCache_->put(encounterId, std::move(textureOpt.value()));
    currentTexture_ = textureCache_->get(encounterId);

    if (!currentTexture_) {
        return MapWorldBounds{};
    }

    currentBounds_ = calculateEncounterBounds(info, currentTexture_->width, currentTexture_->height);

    return currentBounds_;
}
