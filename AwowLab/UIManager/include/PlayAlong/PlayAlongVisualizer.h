#pragma once

#include "PlayAlong/UserCloneActor.h"
#include "PlayAlong/PlayAlongInputHandler.h"
#include "PlayAlong/UIPlayerSelector.h"
#include "PlayAlong/UIPlayAlongCombinedGCD.h"
#include "PlayAlong/UIPlayAlongStatsPanel.h"
#include "PlayAlong/PlayAlongKeybindSettings.h"
#include "PlayAlong/PlayAlongFeedbackSystem.h"
#include "UI/ActorVisibilitySettings.h"
#include "Animation/ActorPositionInterpolator.h"
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>
#include <unordered_map>

// Forward declarations
class Camera3D;
class EventDatabase;
class SpellCastDatabase;
class SpellIconLoader;
class SceneRenderer3D;
class ActorColorGenerator;
class DeathDatabase;
class AnnotationManager;
class AutoAnnotationTriggerManager;
class AnnotationRenderer;
class CombatDatabase;
class AuraDatabase;
class EncounterTextureManager;
class ActorAppearanceSettings;
struct CombatantInfoEvent;
struct MapChangeEvent;
struct DeathEvent;
struct ResurrectEvent;

// Vulkan forward declarations
typedef struct VkDevice_T* VkDevice;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkDescriptorPool_T* VkDescriptorPool;
typedef struct VkQueue_T* VkQueue;
typedef struct VkCommandPool_T* VkCommandPool;

namespace PlayAlong {

// Main visualizer for Play Along mode, separate from MapVisualizer
class PlayAlongVisualizer {
public:
    PlayAlongVisualizer();
    ~PlayAlongVisualizer();

    // Non-copyable
    PlayAlongVisualizer(const PlayAlongVisualizer&) = delete;
    PlayAlongVisualizer& operator=(const PlayAlongVisualizer&) = delete;

    // Initialize Vulkan rendering resources
    bool initialize(VkDevice device, VkPhysicalDevice gpu,
                    VkRenderPass renderPass, VkDescriptorPool descriptorPool,
                    VkQueue graphicsQueue, VkCommandPool commandPool);

    // Load databases (non-owning pointers)
    void loadEventDatabase(const EventDatabase* db);
    void loadSpellCastDatabase(const SpellCastDatabase* db);
    void setSpellIconLoader(SpellIconLoader* loader);

    // Load map change events for floor texture
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges);

    // Load death events for culling dead actors
    void loadDeathEvents(const std::vector<DeathEvent>& deaths,
                         const std::vector<ResurrectEvent>& resurrects,
                         const EventDatabase* eventDb);

    // Load optional databases for annotation system
    void loadAnnotationDatabases(const CombatDatabase* combatDb,
                                  const AuraDatabase* auraDb,
                                  const std::unordered_map<uint32_t, std::string>* spellNameMap);

    // Set encounter info for annotation file loading
    void setEncounterInfo(uint32_t encounterId, const std::string& encounterName);

    // Set the target player to practice as
    void setTargetPlayer(const std::string& guid, const std::string& name);

    // Set combatant info for player selection (uses eventDb to resolve player names)
    void setCombatantInfo(const std::vector<CombatantInfoEvent>& combatants,
                          const EventDatabase* eventDb);

    // Main render loop (ImGui panels)
    void render();

    // 3D rendering (Vulkan command buffer)
    // Called by VulkanRenderer::drawFrameWith3DAndImGui
    void render3D(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height);

    // Navigation flags
    bool shouldReturnToLandingPage() const { return returnToLandingPage_; }
    bool shouldReturnToEncounterSelector() const { return returnToEncounterSelector_; }
    void resetNavigationFlags();

    // Get the player selector for external use
    UIPlayerSelector& getPlayerSelector() { return playerSelector_; }

    // Set keybind settings from modal
    void setKeybindSettings(const PlayAlongKeybindSettings& settings) { keybindSettings_ = settings; }
    const PlayAlongKeybindSettings& getKeybindSettings() const { return keybindSettings_; }

    // Timeline control
    uint32_t getCurrentTimeMs() const { return currentTimeMs_; }
    void setCurrentTimeMs(uint32_t ms) { currentTimeMs_ = ms; }
    bool isPlaying() const { return isPlaying_; }
    void setPlaying(bool playing) { isPlaying_ = playing; }

    // Get min/max time from loaded data
    uint32_t getMinTimeMs() const { return minTimeMs_; }
    uint32_t getMaxTimeMs() const { return maxTimeMs_; }

private:
    void handleInput();
    void updateCamera();
    void renderUI();
    void renderPlaybackControls();
    void renderPositionInfo();
    void renderCountdownOverlay();
    void renderSnapHint();

    // Check for keybind presses and record them
    void checkKeybindPresses();

    // Find next spell cast event with a bound keybind after given time
    // Returns timestamp of next bound cast, or 0 if none found
    uint32_t findNextBoundCastTime(uint32_t afterTimeMs) const;

    // Initialize clone position from target player's first event
    void initializeClonePosition();

    // Get target player's position at current time (for ghost rendering)
    bool getTargetPlayerPosition(float& x, float& y, float& facing) const;

    // Core state
    std::string targetPlayerGuid_;
    std::string targetPlayerName_;

    // User-controlled clone
    UserCloneActor clone_;

    // Input handler (standalone)
    PlayAlongInputHandler inputHandler_;

    // Player selector
    UIPlayerSelector playerSelector_;

    // Camera (follows clone, shared_ptr for SceneRenderer3D compatibility)
    std::shared_ptr<Camera3D> camera3D_;

    // 3D rendering (owns its own instance)
    std::unique_ptr<SceneRenderer3D> sceneRenderer_;
    std::unique_ptr<EncounterTextureManager> encounterTextureManager_;
    std::shared_ptr<ActorColorGenerator> colorGenerator_;

    // Visibility settings for PlayAlong (show only enemies)
    ActorVisibilitySettings visibilitySettings_;

    // Appearance settings for spec icon rendering
    std::shared_ptr<ActorAppearanceSettings> appearanceSettings_;

    // Databases (non-owning pointers)
    const EventDatabase* eventDb_ = nullptr;
    const SpellCastDatabase* spellCastDb_ = nullptr;
    SpellIconLoader* spellIconLoader_ = nullptr;

    // Death database (owned, for culling dead actors)
    std::unique_ptr<DeathDatabase> deathDb_;

    // Combined GCD panel (inputs + expected casts in one movable window)
    UIPlayAlongCombinedGCD combinedGcd_;

    // Keybind settings (loaded from modal, used during playback)
    PlayAlongKeybindSettings keybindSettings_;

    // Feedback system for scoring player inputs
    PlayAlongFeedbackSystem feedbackSystem_;
    UIPlayAlongStatsPanel statsPanel_;
    bool feedbackInitialized_ = false;

    // Playback state
    uint32_t currentTimeMs_ = 0;
    uint32_t previousTimeMs_ = 0;  // For detecting rewind
    uint32_t minTimeMs_ = 0;
    uint32_t maxTimeMs_ = 0;
    bool isPlaying_ = false;
    float lastFrameTime_ = 0.0f;

    // Navigation
    bool returnToLandingPage_ = false;
    bool returnToEncounterSelector_ = false;
    bool showExitConfirmation_ = false;  // Show "Return to home?" popup
    bool wasPlayingBeforeConfirmation_ = false;  // Track play state to restore on cancel
    int exitConfirmationFrameDelay_ = 0;  // Delay before ESC can close popup (prevents instant close)

    // Countdown state (before playback starts)
    bool countdownActive_ = false;
    float countdownTimeRemaining_ = 0.0f;
    static constexpr float COUNTDOWN_DURATION = 3.0f;

    // Snap hint state (shown once per encounter when too far from ghost)
    bool snapHintShown_ = false;
    float snapHintTimeRemaining_ = 0.0f;  // For auto-dismiss
    static constexpr float SNAP_HINT_DISTANCE = 50.0f;
    static constexpr float SNAP_HINT_DURATION = 4.0f;

    // Audio feedback settings
    bool audioFeedbackEnabled_ = true;
    size_t lastFeedbackCount_ = 0;  // Track processed feedback to detect new events
    static constexpr int BEEP_GOOD_FREQ = 800;   // Hz
    static constexpr int BEEP_BAD_FREQ = 300;    // Hz
    static constexpr int BEEP_DURATION_MS = 100;

    // Beep for a good or bad cast
    void playFeedbackSound(FeedbackResult result);

    // Vulkan context (for rendering)
    VkDevice device_ = nullptr;
    VkPhysicalDevice gpu_ = nullptr;
    VkRenderPass renderPass_ = nullptr;
    bool vulkanInitialized_ = false;

    // Cached position data for rendering (using std::string keys for safe ownership)
    std::unordered_map<std::string, InterpolatedPosition> actorPositions_;
    std::unordered_map<std::string, InterpolatedResource> actorResources_;
    std::unordered_map<std::string, std::string> guidToName_;

    // Map change tracking
    std::vector<MapChangeEvent> mapChangeEvents_;
    std::unordered_map<uint16_t, MapChangeEvent> mapIdToData_;
    uint16_t currentMapId_ = 0;

    // Builds actor positions from EventDatabase at the current time
    void buildActorPositions();

    // Picks the floor map from the current actor positions
    void updateMapFromActorPositions();

    // Annotation system
    std::unique_ptr<AnnotationManager> annotationManager_;
    std::unique_ptr<AutoAnnotationTriggerManager> autoAnnotationTriggerManager_;
    std::unique_ptr<AnnotationRenderer> annotationRenderer_;
    bool annotationsEnabled_ = true;
    bool annotationCacheBuilt_ = false;

    // Optional databases for annotation cache building (non-owning)
    const CombatDatabase* combatDb_ = nullptr;
    const AuraDatabase* auraDb_ = nullptr;

    // Encounter info for annotation file loading
    uint32_t encounterId_ = 0;
    std::string encounterName_;
};

} // namespace PlayAlong
