#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <set>
#include <string>
#include <cstdint>
#include <functional>

struct GLFWwindow;
struct ActorEvent;
struct CombatantInfoEvent;
struct MapChangeEvent;
class MapVisualizer;
class EventDatabase;
class SpellCastDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class DispelDatabase;
class AvoidanceDatabase;
class EmoteDatabase;
class AutoAnnotationTriggerManager;

class ImGuiManager {
public:
    ImGuiManager();
    ~ImGuiManager();

    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    bool initialize(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice,
                   VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                   VkRenderPass renderPass, uint32_t minImageCount, uint32_t imageCount);

    // Dynamic CJK font loading - call when loading encounters with CJK player names
    // Scans the provided strings for CJK characters and rebuilds font atlas if needed
    // Returns true if fonts were rebuilt
    bool loadCjkFontsForStrings(const std::vector<std::string>& strings);

    // Check if current locale requires full CJK font support
    static bool isCjkLocale(const std::string& locale);

    // Scan strings for CJK characters, returns set of unique codepoints
    static std::set<uint32_t> scanForCjkCharacters(const std::vector<std::string>& strings);

    void cleanup(VkDevice device);

    void newFrame(bool renderMapVisualizer = true);
    void render(VkCommandBuffer commandBuffer);

    // 3D rendering
    bool initialize3DRendering(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                               VkQueue graphicsQueue, VkCommandPool commandPool);
    bool initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                             VkQueue graphicsQueue, VkCommandPool commandPool);
    void render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent);

    // UI state
    bool& getColorToggle() { return isWhite; }

    // Combat log visualization
    void loadEventDatabase(const EventDatabase* db);
    void loadSpellCastDatabase(const SpellCastDatabase* db);
    bool loadSpellGCDData(const std::string& csvPath);
    void loadResourceDatabase(const ResourceDatabase* db);
    void loadCombatDatabase(const CombatDatabase* db);
    void loadDeathDatabase(const DeathDatabase* db);
    void loadAuraDatabase(const AuraDatabase* db);
    void loadDispelDatabase(const DispelDatabase* db);
    void loadAvoidanceDatabase(const AvoidanceDatabase* db);
    void loadEmoteDatabase(const EmoteDatabase* db);
    void loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo);
    void generateFallbackColors();
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId = 0);
    void loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap);
    void loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap);
    // WCL actor id -> creature id (their GUIDs encode the actor id, not the
    // npc id). Empty for real combat logs.
    void loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId);
    void loadHealthScalingData(uint64_t avgPlayerHealth,
                               const std::unordered_map<std::string, uint64_t>& actorMaxHealth);
    void setExtractedData(const struct ExtractedData* data);
    void setExportCallback(std::function<void(
        const struct ExtractedData*,
        const std::vector<struct Annotation>&,
        const std::vector<struct AutoAnnotationTrigger>&,
        const std::vector<struct CachedAnnotationEvent>&,
        int32_t, int32_t)> callback);

    // Navigation control
    bool shouldReturnToEncounterSelector() const;
    bool shouldReturnToLandingPage() const;
    bool shouldRefreshEncounters() const;
    void resetNavigationFlags();
    void resetRefreshFlag();

    // Unload databases before switching encounters
    void unloadDatabases();

    // Get spell icon loader for external use
    class SpellIconLoader* getSpellIconLoader();

    // Get auto-annotation trigger manager for direct access (e.g., snapshot import)
    AutoAnnotationTriggerManager* getAutoAnnotationTriggerManager();

    // Process deferred texture deletions - call once per frame
    // Ensures textures aren't destroyed while still in use by GPU
    void processTextureCleanup();

private:
    VkDescriptorPool descriptorPool;
    bool isWhite;
    std::unique_ptr<MapVisualizer> mapVisualizer;

    // Vulkan context for font atlas rebuild
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;

    // Track which CJK codepoints are currently loaded (empty = none loaded dynamically)
    std::set<uint32_t> loadedCjkCodepoints_;

    // Whether full CJK was loaded at startup (for CJK locale)
    bool fullCjkLoaded_ = false;

    // Helper to rebuild font atlas with specific CJK codepoints
    void rebuildFontAtlas(const std::set<uint32_t>& cjkCodepoints);
};
