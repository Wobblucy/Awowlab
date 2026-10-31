#include "UI/ClassIconCache.h"
#include "Vulkan/Textures/VulkanTextureManager.h"
#include "Core/ErrorLogger.h"
#include <set>

namespace {
    // Map WCL class names to icon filenames
    const std::unordered_map<std::string, std::string> CLASS_ICON_MAP = {
        {"DeathKnight", "classicon_deathknight.png"},
        {"Death Knight", "classicon_deathknight.png"},
        {"DemonHunter", "classicon_demonhunter.png"},
        {"Demon Hunter", "classicon_demonhunter.png"},
        {"Druid", "classicon_druid.png"},
        {"Evoker", "classicon_evoker.png"},
        {"Hunter", "classicon_hunter.png"},
        {"Mage", "classicon_mage.png"},
        {"Monk", "classicon_monk.png"},
        {"Paladin", "classicon_paladin.png"},
        {"Priest", "classicon_priest.png"},
        {"Rogue", "classicon_rogue.png"},
        {"Shaman", "classicon_shaman.png"},
        {"Warlock", "classicon_warlock.png"},
        {"Warrior", "classicon_warrior.png"},
    };
}

ClassIconCache::ClassIconCache() = default;

ClassIconCache::~ClassIconCache() {
    cleanup();
}

bool ClassIconCache::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                 VkQueue graphicsQueue, VkCommandPool commandPool,
                                 const std::string& iconBasePath) {
    if (initialized_) {
        return true;
    }

    device_ = device;

    // Create texture manager (VK_NULL_HANDLE for descriptor pool is fine - not used)
    textureManager_ = std::make_unique<VulkanTextureManager>();
    if (!textureManager_->initialize(device, physicalDevice, graphicsQueue,
                                      commandPool, VK_NULL_HANDLE)) {
        AWOW_LOG_ERROR("TEXTURE", "ClassIconCache: Failed to initialize texture manager");
        return false;
    }

    loadIcons(iconBasePath);

    initialized_ = true;
    return true;
}

void ClassIconCache::loadIcons(const std::string& basePath) {
    if (!textureManager_) {
        return;
    }

    std::set<std::string> loadedFiles;  // Track unique files to avoid duplicate loads

    for (const auto& [className, filename] : CLASS_ICON_MAP) {
        // Skip if file already loaded (handles duplicate spellings like "DeathKnight" and "Death Knight")
        if (loadedFiles.count(filename) > 0) {
            // Just map the alternative name to the same texture
            auto baseIt = iconCache_.find(filename);
            if (baseIt != iconCache_.end()) {
                iconCache_[className] = baseIt->second;
            }
            continue;
        }

        std::string filepath = basePath + "/" + filename;
        std::string debugName = "ClassIcon_" + className;
        auto texture = textureManager_->loadTextureFromFile(filepath, debugName.c_str());
        if (texture) {
            iconCache_[className] = *texture;
            iconCache_[filename] = *texture;  // Also store by filename for lookup
            loadedFiles.insert(filename);
        }
    }

    AWOW_LOG_INFO("TEXTURE", "ClassIconCache: Loaded " + std::to_string(loadedFiles.size()) + " class icons");
}

ImTextureID ClassIconCache::getClassIcon(const std::string& className) {
    auto it = iconCache_.find(className);
    if (it != iconCache_.end() && it->second.isValid()) {
        return reinterpret_cast<ImTextureID>(it->second.descriptorSet);
    }
    return reinterpret_cast<ImTextureID>(VK_NULL_HANDLE);
}

void ClassIconCache::cleanup() {
    if (!initialized_ || !textureManager_ || device_ == VK_NULL_HANDLE) {
        return;
    }

    // Track already-destroyed images to avoid double-destroy
    // (same texture is stored under multiple keys: class name and filename)
    std::set<VkImage> destroyedImages;
    for (auto& [name, texture] : iconCache_) {
        if (texture.isValid() && destroyedImages.count(texture.image) == 0) {
            destroyedImages.insert(texture.image);
            textureManager_->destroyTexture(texture);
        }
    }
    iconCache_.clear();

    // Flush pending texture deletions before destroying the manager
    // Without this, textures in the deletion queue are never destroyed
    textureManager_->flushDeletionQueue(true);
    textureManager_.reset();
    initialized_ = false;
}
