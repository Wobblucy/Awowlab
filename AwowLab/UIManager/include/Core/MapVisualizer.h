#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <functional>
#include <vulkan/vulkan.h>
#include <imgui.h>

class EventDatabase;
class SpellCastDatabase;
class SpellGCDDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class DispelDatabase;
class AvoidanceDatabase;
class EmoteDatabase;
struct ActorEvent;
struct CombatantInfoEvent;
struct MapChangeEvent;
struct WindowState;
struct ExtractedData;

// Forward declarations for components
class FrameDataProvider;
class RenderingCoordinator;
class UIWindowManager;
class ActorColorGenerator;
class PlaybackController;
class Camera3D;
class ActorPositionInterpolator;
class CameraController;
class MouseRaycaster;
class AnnotationManager;
class UIAnnotationContextMenu;
class UIAnnotationPropertiesPanel;
class VulkanTextureManager;
class UIAnnotationListPanel;
class AutoAnnotationTriggerManager;
class UIAutoAnnotationPanel;
class MapChangeController;
class EncounterTextureManager;
struct HostileCreatureSpells;
struct Annotation;
struct AutoAnnotationTrigger;
struct CachedAnnotationEvent;

// Callback type for snapshot export requests
using ExportSnapshotCallback = std::function<void(
    const ExtractedData* data,
    const std::vector<Annotation>& annotations,
    const std::vector<AutoAnnotationTrigger>& triggers,
    const std::vector<CachedAnnotationEvent>& cachedEvents,
    int32_t startTimeMs,
    int32_t endTimeMs
)>;

class MapVisualizer {
public:
    MapVisualizer();
    ~MapVisualizer();

    MapVisualizer(const MapVisualizer&) = delete;
    MapVisualizer& operator=(const MapVisualizer&) = delete;

    // Initialize 3D rendering
    bool initialize3D(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                      VkQueue graphicsQueue, VkCommandPool commandPool);

    // Initialize spell icon loader
    bool initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkQueue graphicsQueue, VkCommandPool commandPool,
                              VkDescriptorPool descriptorPool);

    // Cleanup 3D resources
    void cleanup3D(VkDevice device);

    // Load event database reference
    void loadEventDatabase(const EventDatabase* db);

    // Load spell cast database reference
    void loadSpellCastDatabase(const SpellCastDatabase* db);

    // Load spell GCD data from CSV file
    bool loadSpellGCDData(const std::string& csvPath);

    // Load resource database reference (health/power tracking)
    void loadResourceDatabase(const ResourceDatabase* db);

    // Load combat database reference (damage/healing metrics)
    void loadCombatDatabase(const CombatDatabase* db);

    // Load death database reference (death/resurrection tracking)
    void loadDeathDatabase(const DeathDatabase* db);

    // Load aura database reference (buff/debuff tracking)
    void loadAuraDatabase(const AuraDatabase* db);

    // Load dispel database reference (dispel/interrupt tracking)
    void loadDispelDatabase(const DispelDatabase* db);

    // Load avoidance database reference (dodge/parry/block/miss tracking)
    void loadAvoidanceDatabase(const AvoidanceDatabase* db);

    // Load emote database reference (boss emote lines for phase rules)
    void loadEmoteDatabase(const EmoteDatabase* db);

    // Load combatant info (spec IDs) for role-based rendering
    void loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo);

    // Generate fallback colors for actors without COMBATANT_INFO (NPCs, pets, etc.)
    // Call after loadCombatantInfo() so player class colors are already set
    void generateFallbackColors();

    // Load map change events for worldmap floor textures
    // encounterId: optional encounter ID for encounter-specific texture loading (0 = use mapId-based)
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId = 0);

    // Load spell name map for UI display
    void loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap);

    // Load GUID to name map for resolving pet/guardian names
    void loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap);
    // WCL actor id -> creature id (their GUIDs encode the actor id, not the
    // npc id). Empty for real combat logs.
    void loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId);

    // Load health scaling data for mob size scaling
    void loadHealthScalingData(uint64_t avgPlayerHealth,
                               const std::unordered_map<std::string, uint64_t>& actorMaxHealth);

    // Store ExtractedData reference for snapshot export
    void setExtractedData(const ExtractedData* data);

    // Set callback for snapshot export requests
    void setExportCallback(ExportSnapshotCallback callback);

    // Render the ImGui UI
    void render();

    // Render the 3D scene
    void render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent);

    // Timeline control (delegates to PlaybackController)
    // Supports negative timestamps for pre-encounter events
    void setTimeRange(int32_t start_ms, int32_t end_ms);
    void setCurrentTime(int32_t time_ms);
    int32_t getCurrentTime() const;
    int32_t getMaxTime() const;

    // Playback controls (delegates to PlaybackController)
    void play();
    void pause();
    void togglePlayPause();
    bool isPlaybackActive() const;
    void setPlaybackSpeed(float speed);

    // Navigation control
    bool shouldReturnToEncounterSelector() const;
    bool shouldReturnToLandingPage() const;
    bool shouldRefreshEncounters() const;
    void resetNavigationFlags();
    void resetRefreshFlag();

    // Unload databases before switching encounters
    void unloadDatabases();

    // Get spell icon loader for external use (e.g., PlayAlong mode)
    class SpellIconLoader* getSpellIconLoader();

    // Get auto-annotation trigger manager for direct access (e.g., snapshot import)
    AutoAnnotationTriggerManager* getAutoAnnotationTriggerManager();

    // Process deferred texture deletions - call once per frame
    void processTextureCleanup();

private:
    // Core subsystems
    std::unique_ptr<FrameDataProvider> frameDataProvider_;
    std::unique_ptr<RenderingCoordinator> renderingCoordinator_;
    std::unique_ptr<UIWindowManager> uiWindowManager_;

    // Shared components
    std::shared_ptr<ActorColorGenerator> colorGenerator_;
    std::shared_ptr<PlaybackController> playbackController_;
    std::shared_ptr<Camera3D> camera3D_;
    std::shared_ptr<ActorPositionInterpolator> positionInterpolator_;
    std::shared_ptr<CameraController> cameraController_;
    std::unique_ptr<MouseRaycaster> mouseRaycaster_;

    // Spell GCD database (owned)
    std::unique_ptr<SpellGCDDatabase> spellGCDDatabase_;

    // Annotation system
    std::unique_ptr<AnnotationManager> annotationManager_;
    std::unique_ptr<UIAnnotationContextMenu> annotationContextMenu_;
    std::unique_ptr<UIAnnotationPropertiesPanel> annotationPropertiesPanel_;
    std::unique_ptr<UIAnnotationListPanel> annotationListPanel_;

    // Auto-annotation system
    std::unique_ptr<AutoAnnotationTriggerManager> autoAnnotationTriggerManager_;
    std::unique_ptr<UIAutoAnnotationPanel> autoAnnotationPanel_;
    std::vector<HostileCreatureSpells> hostileCreatureSpells_;

    // Map change controller (handles map switching with anti-flicker)
    std::unique_ptr<MapChangeController> mapChangeController_;

    // Encounter texture manager (handles per-encounter floor textures)
    std::unique_ptr<EncounterTextureManager> encounterTextureManager_;

    // Texture manager for custom model (MTL) textures. The shape registry
    // owns it too; kept here so its deletion queue gets drained each frame.
    std::shared_ptr<VulkanTextureManager> customModelTextureManager_;

    // Right-click tracking for context menu vs camera rotation
    ImVec2 rightClickStartPos_;
    bool rightClickStarted_ = false;
    static constexpr float CLICK_VS_DRAG_THRESHOLD = 5.0f;

    // Left-click tracking for actor selection vs camera panning
    ImVec2 leftClickStartPos_;
    bool leftClickStarted_ = false;

    // Cached extent from render3D for use in render()
    VkExtent2D cachedExtent_ = {0, 0};

    // Frame timing for playback
    double lastFrameTime_;

    // Navigation flags
    bool shouldReturnToEncounterSelector_;
    bool shouldReturnToLandingPage_;
    bool shouldRefreshEncounters_;

    // Spell name map for UI display (stored here for lifetime management)
    std::unordered_map<uint32_t, std::string> spellNameMap_;

    // GUID to name map for resolving pet/guardian names (stored here for lifetime management)
    std::unordered_map<std::string, std::string> guidToNameMap_;

    // ExtractedData reference for snapshot export (non-owning, must outlive this object)
    const ExtractedData* extractedData_ = nullptr;

    // Export callback for snapshot requests
    ExportSnapshotCallback exportCallback_;

    // Helper functions
    void handleCameraInput();
    void renderAnnotationPanels();
    void handleMouseInput();
    void handleKeyboardShortcuts();
    void selectActorAt(const ImVec2& screenPos);
    void openAnnotationMenuAt(const ImVec2& screenPos);
    void renderMinimapWindow();
    void updateMinimapBounds();
    void centerCameraOnEncounter();
    std::string getSelectedActorGuid() const;
    // Nearest actor whose screen position is within the pick radius. With
    // renderedActorsOnly, actors hidden by the visibility settings are skipped.
    std::optional<std::string> findNearestActorAtScreenPos(const ImVec2& screenPos,
                                                           bool renderedActorsOnly) const;

    // Build hostile creature spells from databases (for auto-annotation config)
    void buildHostileCreatureSpells();
};
