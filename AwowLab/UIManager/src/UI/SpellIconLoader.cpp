#include "UI/SpellIconLoader.h"
#include "UI/SpellIconTable.h"
#include "UI/SpellContextMenu.h"
#include "Core/ErrorLogger.h"
#include "Core/Utils/ExecutablePath.h"
#include "UI/SpellLinkOpener.h"
#include "stb_image.h"  // Decode-only use; implementation lives in VulkanTextureManager.cpp
#include <fstream>
#include <algorithm>

#include <cstring>
#include <filesystem>
#include <unordered_set>

namespace {

// Rejects icon filenames that could escape the icon folder, since the
// mapping file could contain paths like "../../secrets.txt".
bool isValidIconFilename(const std::string& filename) {
    if (filename.empty()) return false;

    if (filename.find("..") != std::string::npos) return false;

    if (filename[0] == '/' || filename[0] == '\\') return false;
    if (filename.length() >= 2 && std::isalpha(static_cast<unsigned char>(filename[0])) && filename[1] == ':') {
        return false;  // Windows drive letter
    }

    if (filename.find('\0') != std::string::npos) return false;

    return true;
}

} // anonymous namespace

SpellIconLoader::SpellIconLoader()
    : textureManager_(nullptr)
    , textureCache_(nullptr) {
}

SpellIconLoader::~SpellIconLoader() {
    cleanup();
}

bool SpellIconLoader::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                  VkQueue graphicsQueue, VkCommandPool commandPool,
                                  VkDescriptorPool descriptorPool) {
    textureManager_ = std::make_unique<VulkanTextureManager>();
    if (!textureManager_->initialize(device, physicalDevice, graphicsQueue,
                                      commandPool, descriptorPool)) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to initialize texture manager");
        return false;
    }

    textureCache_ = std::make_unique<LRUCache<uint32_t, TextureHandle>>(
        MAX_CACHED_TEXTURES,
        [this](const uint32_t& /*spellId*/, TextureHandle& texture) {
            if (textureManager_) {
                textureManager_->destroyTexture(texture);
            }
        });

    iconBasePath_ = awow::getExecutableDirectory() + "PNG\\Spell Icons\\";

    // Create a default "unknown spell" texture (solid gray square)
    auto defaultOpt = textureManager_->createSolidColorTexture(64, 64, 128, 128, 128, 255,
                                                                "SpellIcon_Default");
    if (!defaultOpt.has_value()) {
        AWOW_LOG_ERROR("TEXTURE", "Failed to create default spell icon texture");
        return false;
    }
    defaultTexture_ = defaultOpt.value();

    return true;
}

void SpellIconLoader::cleanup() {
    // Stop the decode worker before tearing down anything it may feed
    stopDecodeWorker();

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
        textureManager_->flushDeletionQueue(true);  // Skip ImGui since we're cleaning up
    }

    textureManager_.reset();
}

void SpellIconLoader::processTextureCleanup() {
    // Turn any freshly decoded icons into textures first - this runs
    // once per frame on the render thread, which owns the command pool
    uploadFinishedIcons();

    if (textureManager_) {
        textureManager_->processDeletionQueue();
    }
}

void SpellIconLoader::startDecodeWorker() {
    if (decodeWorkerRunning_) {
        return;
    }
    decodeQuit_.store(false);
    decodeThread_ = std::thread([this]() { decodeWorkerLoop(); });
    decodeWorkerRunning_ = true;
}

void SpellIconLoader::stopDecodeWorker() {
    if (!decodeWorkerRunning_) {
        return;
    }
    decodeQuit_.store(true);
    decodeCv_.notify_all();
    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    decodeWorkerRunning_ = false;
    decodeJobs_.clear();
    decodedImages_.clear();
    iconsInFlight_.clear();
}

void SpellIconLoader::decodeWorkerLoop() {
    for (;;) {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(decodeMutex_);
            decodeCv_.wait(lock, [this]() { return decodeQuit_.load() || !decodeJobs_.empty(); });
            if (decodeQuit_.load()) {
                return;
            }
            job = std::move(decodeJobs_.front());
            decodeJobs_.pop_front();
        }

        DecodedImage result;
        result.spellId = job.spellId;

        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(job.filepath.c_str(), &width, &height,
                                          &channels, STBI_rgb_alpha);
        if (pixels) {
            result.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
            result.width = width;
            result.height = height;
            stbi_image_free(pixels);
        }

        {
            std::lock_guard<std::mutex> lock(decodeMutex_);
            decodedImages_.push_back(std::move(result));
        }
    }
}

void SpellIconLoader::uploadFinishedIcons() {
    if (!textureManager_ || !textureCache_) {
        return;
    }

    std::vector<DecodedImage> finished;
    {
        std::lock_guard<std::mutex> lock(decodeMutex_);
        if (decodedImages_.empty()) {
            return;
        }
        finished.swap(decodedImages_);
    }

    for (auto& image : finished) {
        iconsInFlight_.erase(image.spellId);

        if (image.pixels.empty()) {
            // Decode failed - remember so the render path stops asking
            failedIcons_.insert(image.spellId);
            continue;
        }

        std::string debugName = "SpellIcon_" + std::to_string(image.spellId);
        auto textureOpt = textureManager_->createTexture(
            image.pixels.data(), image.width, image.height, debugName.c_str());
        if (textureOpt.has_value()) {
            textureCache_->put(image.spellId, std::move(textureOpt.value()));
        } else {
            failedIcons_.insert(image.spellId);
        }
    }
}

void SpellIconLoader::setBinaryMappingPath(const std::string& binaryFilePath) {
    binaryMappingPath_ = binaryFilePath;
}

bool SpellIconLoader::loadBinaryFile() {
    if (binaryLoaded_) {
        return true;
    }

    if (binaryMappingPath_.empty()) {
        AWOW_LOG_ERROR("ICONS", "Binary mapping path is empty");
        return false;
    }

    std::ifstream file(binaryMappingPath_, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("ICONS", "Failed to open binary mapping file: " + binaryMappingPath_);
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize < 0) {
        AWOW_LOG_ERROR("ICONS", "Failed to size binary mapping file: " + binaryMappingPath_);
        return false;
    }

    binaryData_.resize(static_cast<size_t>(fileSize));
    if (!file.read(binaryData_.data(), fileSize)) {
        AWOW_LOG_ERROR("ICONS", "Failed to read binary mapping file: " + binaryMappingPath_);
        binaryData_.clear();
        return false;
    }

    // Validate header and table sizes once, so the lookups below can trust them
    std::string layoutError;
    auto layout = spell_icon_table::parse(binaryData_.data(), binaryData_.size(), layoutError);
    if (!layout) {
        AWOW_LOG_ERROR("ICONS", layoutError);
        binaryData_.clear();
        return false;
    }
    stringTable_ = std::move(layout->filenames);
    mappingTableOffset_ = layout->mappingOffset;
    mappingCount_ = layout->numMappings;

    binaryLoaded_ = true;
    return true;
}

SpellIconLoader::IconMapping SpellIconLoader::readMapping(size_t index) const {
    const char* entry = binaryData_.data() + mappingTableOffset_ +
                        index * spell_icon_table::kMappingSize;
    IconMapping mapping;
    std::memcpy(&mapping.spellId, entry, sizeof(mapping.spellId));
    std::memcpy(&mapping.filenameIndex, entry + sizeof(mapping.spellId),
                sizeof(mapping.filenameIndex));
    return mapping;
}

std::string SpellIconLoader::lookupFilenameInBinary(uint32_t spellId) {
    if (!binaryLoaded_ || binaryData_.empty()) {
        return "";
    }

    // Binary search in mapping table (sorted by spell_id)
    size_t left = 0;
    size_t right = mappingCount_;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const IconMapping entry = readMapping(mid);

        if (entry.spellId == spellId) {
            if (entry.filenameIndex < stringTable_.size()) {
                return stringTable_[entry.filenameIndex];
            }
            return "";
        } else if (entry.spellId < spellId) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return "";  // Not found
}

size_t SpellIconLoader::loadMappingsForIds(const std::unordered_set<uint32_t>& spellIds) {
    if (!loadBinaryFile()) {
        return 0;
    }

    // Filter to only IDs we don't already have cached
    std::unordered_set<uint32_t> neededIds;
    for (uint32_t id : spellIds) {
        if (spellToFilename_.find(id) == spellToFilename_.end()) {
            neededIds.insert(id);
        }
    }

    if (neededIds.empty()) {
        return 0;
    }

    size_t newCount = 0;
    for (uint32_t spellId : neededIds) {
        std::string filename = lookupFilenameInBinary(spellId);
        if (!filename.empty()) {
            spellToFilename_[spellId] = filename;
            newCount++;
        }
    }

    return newCount;
}

bool SpellIconLoader::loadAllMappings() {
    if (!loadBinaryFile()) {
        return false;
    }

    // Header counts were checked against the file size in loadBinaryFile
    for (uint32_t i = 0; i < mappingCount_; i++) {
        const IconMapping entry = readMapping(i);
        if (entry.filenameIndex < stringTable_.size()) {
            spellToFilename_[entry.spellId] = stringTable_[entry.filenameIndex];
        }
    }

    return true;
}

void SpellIconLoader::preloadSpellIcons(const std::vector<uint32_t>& spellIds) {
    if (!textureManager_ || !textureCache_) {
        return;
    }

    for (uint32_t spellId : spellIds) {
        // Skip if already loaded
        if (textureCache_->contains(spellId)) {
            continue;
        }

        auto mappingIt = spellToFilename_.find(spellId);
        if (mappingIt == spellToFilename_.end()) {
            continue;
        }

        if (!isValidIconFilename(mappingIt->second)) {
            continue;
        }

        std::string filepath = iconBasePath_ + mappingIt->second;
        std::string debugName = "SpellIcon_" + std::to_string(spellId);
        auto textureOpt = textureManager_->loadTextureFromFile(filepath, debugName.c_str());

        if (textureOpt.has_value()) {
            textureCache_->put(spellId, textureOpt.value());
        }
    }
}

bool SpellIconLoader::hasRealIcon(uint32_t spellId) const {
    // Already known bad, so the answer will not change this session.
    if (failedIcons_.count(spellId)) return false;

    // Test the mapping, not the texture: an icon still decoding will resolve
    // shortly, while one with no mapping never will.
    return spellToFilename_.count(spellId) > 0;
}

ImTextureID SpellIconLoader::getSpellIcon(uint32_t spellId) {
    if (!textureManager_ || !textureCache_) {
        return getDefaultIcon();
    }

    // Check if already loaded (updates LRU order)
    TextureHandle* cached = textureCache_->get(spellId);
    if (cached) {
        return VulkanTextureManager::getImGuiTextureID(*cached);
    }

    // Known-bad icons and ones already being decoded keep the default
    // icon; retrying every frame would just hammer the disk
    if (failedIcons_.count(spellId) || iconsInFlight_.count(spellId)) {
        return getDefaultIcon();
    }

    auto mappingIt = spellToFilename_.find(spellId);
    if (mappingIt == spellToFilename_.end()) {
        failedIcons_.insert(spellId);
        return getDefaultIcon();
    }

    if (!isValidIconFilename(mappingIt->second)) {
        failedIcons_.insert(spellId);
        return getDefaultIcon();
    }

    // Hand the PNG decode to the worker thread instead of stalling the
    // frame; the texture lands in the cache a frame or two later via
    // uploadFinishedIcons(). Until then the default icon fills in.
    startDecodeWorker();
    iconsInFlight_.insert(spellId);
    {
        std::lock_guard<std::mutex> lock(decodeMutex_);
        decodeJobs_.push_back(DecodeJob{spellId, iconBasePath_ + mappingIt->second});
    }
    decodeCv_.notify_one();

    return getDefaultIcon();
}

ImTextureID SpellIconLoader::getDefaultIcon() {
    return VulkanTextureManager::getImGuiTextureID(defaultTexture_);
}

bool SpellIconLoader::hasIconMapping(uint32_t spellId) const {
    return spellToFilename_.find(spellId) != spellToFilename_.end();
}

std::string SpellIconLoader::getSpellIconFilename(uint32_t spellId) const {
    auto it = spellToFilename_.find(spellId);
    if (it != spellToFilename_.end()) {
        return it->second;
    }
    return "[No mapping]";
}

void SpellIconLoader::checkWowheadLookup(uint32_t spellId) {
    // Ctrl+click opens the spell page; does nothing when no link opener
    // is registered (offline builds like the overlay)
    const auto& openSpellPage = ui::spellLinkOpener();
    if (!openSpellPage) {
        return;
    }
    if (ImGui::IsItemHovered() && ImGui::IsItemClicked() && ImGui::GetIO().KeyCtrl) {
        openSpellPage(spellId);
    }
}

void SpellIconLoader::checkWowheadLookupRect(uint32_t spellId, ImVec2 min, ImVec2 max) {
    // Ctrl+click within the given rect (for ImDrawList::AddImage cases)
    const auto& openSpellPage = ui::spellLinkOpener();
    if (!openSpellPage) {
        return;
    }
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x >= min.x && mousePos.x <= max.x &&
        mousePos.y >= min.y && mousePos.y <= max.y &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetIO().KeyCtrl) {
        openSpellPage(spellId);
    }
}

bool SpellIconLoader::renderSpellIcon(uint32_t spellId, ImVec2 size) {
    ImTextureID icon = getSpellIcon(spellId);
    if (icon != 0) {
        ImGui::Image(icon, size);
        checkWowheadLookup(spellId);
        return true;
    }
    return false;
}

bool SpellIconLoader::checkSpellContextMenu(uint32_t spellId, const std::string& spellName,
                                              int32_t timestamp_ms, bool isHostile,
                                              const std::vector<int32_t>& groupedTimestamps,
                                              std::string_view actorGuid) {
    return SpellContextMenu::checkAndShow(spellId, spellName, timestamp_ms, isHostile, groupedTimestamps, actorGuid);
}

bool SpellIconLoader::checkSpellContextMenuRect(uint32_t spellId, const std::string& spellName,
                                                 int32_t timestamp_ms, bool isHostile,
                                                 ImVec2 min, ImVec2 max,
                                                 const std::vector<int32_t>& groupedTimestamps,
                                                 std::string_view actorGuid) {
    return SpellContextMenu::checkAndShowRect(spellId, spellName, timestamp_ms, isHostile, min, max, groupedTimestamps, actorGuid);
}
