#pragma once
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <memory>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "../../Core/LRUCache.h"
#include "Vulkan/Textures/VulkanTextureManager.h"
#include "UI/ISpellIconRenderer.h"

// Manages spell icon textures for ImGui rendering
// Uses VulkanTextureManager for texture creation and LRUCache for caching
// Implements ISpellIconRenderer so AnnotationManager can render icons through the interface
class SpellIconLoader : public awow::ISpellIconRenderer {
public:
    SpellIconLoader();
    ~SpellIconLoader();

    SpellIconLoader(const SpellIconLoader&) = delete;
    SpellIconLoader& operator=(const SpellIconLoader&) = delete;

    // Initialize with Vulkan context
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkQueue graphicsQueue, VkCommandPool commandPool,
                   VkDescriptorPool descriptorPool);

    // Cleanup resources
    void cleanup();

    // Set path to spell mapping binary file (lazy loading - doesn't load anything yet)
    void setBinaryMappingPath(const std::string& binaryFilePath);

    // Load mappings only for specific spell IDs from binary file (lazy loading)
    // Only reads entries for IDs not already cached
    // Returns number of new mappings loaded
    size_t loadMappingsForIds(const std::unordered_set<uint32_t>& spellIds);

    // Load every spell ID -> filename mapping from the binary file
    bool loadAllMappings();

    // Pre-load icons for a list of spell IDs (called at startup for replay spells)
    void preloadSpellIcons(const std::vector<uint32_t>& spellIds);

    // Get texture ID for a spell (loads on-demand if not cached)
    // Implements ISpellIconRenderer interface
    ImTextureID getSpellIcon(uint32_t spellId) override;

    // Whether this spell has an icon of its own, as opposed to falling back
    // to the generic placeholder.
    //
    // getSpellIcon never returns a null handle - a spell with no artwork gets
    // the default gear - so a caller that wants to draw something else instead
    // has no way to tell the two apart from the handle alone. Anything with a
    // readable fallback (a name, an abbreviation) should ask here first.
    bool hasRealIcon(uint32_t spellId) const;

    // Check for Ctrl+click on last ImGui item and open Wowhead spell page
    // Call immediately after ImGui::Image() for the spell icon
    static void checkWowheadLookup(uint32_t spellId);

    // Check for Ctrl+click within a specific rect (for ImDrawList::AddImage cases)
    static void checkWowheadLookupRect(uint32_t spellId, ImVec2 min, ImVec2 max);

    // Check for right-click on last ImGui item to show spell context menu
    // Call immediately after ImGui::Image() for the spell icon
    // Returns true if context menu was opened
    // Optional: groupedTimestamps - if non-empty, "all instances" will use these instead of querying database
    // Optional: actorGuid - if provided, used for spec lookup when opening spell settings
    static bool checkSpellContextMenu(uint32_t spellId, const std::string& spellName,
                                       int32_t timestamp_ms, bool isHostile,
                                       const std::vector<int32_t>& groupedTimestamps = {},
                                       std::string_view actorGuid = {});

    // Check for right-click within a specific rect (for ImDrawList::AddImage cases)
    // Optional: groupedTimestamps - if non-empty, "all instances" will use these instead of querying database
    // Optional: actorGuid - if provided, used for spec lookup when opening spell settings
    static bool checkSpellContextMenuRect(uint32_t spellId, const std::string& spellName,
                                           int32_t timestamp_ms, bool isHostile,
                                           ImVec2 min, ImVec2 max,
                                           const std::vector<int32_t>& groupedTimestamps = {},
                                           std::string_view actorGuid = {});

    // Render spell icon with automatic Ctrl+click Wowhead lookup
    // Returns true if icon was rendered (for SameLine() logic)
    // Implements ISpellIconRenderer interface
    bool renderSpellIcon(uint32_t spellId, ImVec2 size) override;

    // Get default "unknown spell" icon
    ImTextureID getDefaultIcon();

    // Check if a spell has an icon mapping
    bool hasIconMapping(uint32_t spellId) const;

    // Get the PNG filename for a spell ID (for debugging)
    std::string getSpellIconFilename(uint32_t spellId) const;

    // Process deferred texture deletions - call once per frame
    // This ensures textures aren't destroyed while still in use by GPU
    void processTextureCleanup();

private:
    // Texture manager for Vulkan operations
    std::unique_ptr<VulkanTextureManager> textureManager_;

    // Spell ID -> PNG filename mapping
    std::unordered_map<uint32_t, std::string> spellToFilename_;

    // LRU cache of loaded textures (spell ID -> texture handle)
    // Automatically evicts least recently used textures when full
    std::unique_ptr<LRUCache<uint32_t, TextureHandle>> textureCache_;

    // Default icon for unknown spells
    TextureHandle defaultTexture_;

    // Base path for icon files (set at runtime relative to executable)
    std::string iconBasePath_;

    // Path to spell mapping binary file (for lazy loading)
    std::string binaryMappingPath_;

    // Cached binary file data for efficient lookups
    std::vector<char> binaryData_;
    std::vector<std::string> stringTable_;  // Indexed by filename_index
    size_t mappingTableOffset_ = 0;  // Offset to mapping table in binaryData_
    uint32_t mappingCount_ = 0;      // Entries in the mapping table
    bool binaryLoaded_ = false;

    // Helper methods for binary loading
    bool loadBinaryFile();
    std::string lookupFilenameInBinary(uint32_t spellId);

    // One entry of the mapping table; index must be below mappingCount_
    struct IconMapping {
        uint32_t spellId;
        uint16_t filenameIndex;
    };
    IconMapping readMapping(size_t index) const;

    // Async icon loading. Cache misses during rendering queue a decode
    // job instead of reading the PNG on the render thread; a worker
    // decodes the file and the render thread turns finished pixel
    // buffers into Vulkan textures once per frame (uploads must stay on
    // the thread that owns the command pool). Filenames are resolved at
    // enqueue time so the worker never touches spellToFilename_.
    struct DecodeJob {
        uint32_t spellId = 0;
        std::string filepath;
    };
    struct DecodedImage {
        uint32_t spellId = 0;
        std::vector<unsigned char> pixels;  // Empty = decode failed
        int width = 0;
        int height = 0;
    };

    void startDecodeWorker();
    void stopDecodeWorker();
    void decodeWorkerLoop();
    void uploadFinishedIcons();

    std::thread decodeThread_;
    std::mutex decodeMutex_;
    std::condition_variable decodeCv_;
    std::deque<DecodeJob> decodeJobs_;          // Guarded by decodeMutex_
    std::vector<DecodedImage> decodedImages_;   // Guarded by decodeMutex_
    std::atomic<bool> decodeQuit_{false};
    bool decodeWorkerRunning_ = false;

    // Render-thread-only bookkeeping
    std::unordered_set<uint32_t> iconsInFlight_;  // Queued, not yet uploaded
    std::unordered_set<uint32_t> failedIcons_;    // Don't retry every frame

    // Max cached textures (to avoid memory explosion)
    static constexpr size_t MAX_CACHED_TEXTURES = 200;
};
