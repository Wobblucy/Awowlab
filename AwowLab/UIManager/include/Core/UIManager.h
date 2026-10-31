#pragma once
#include "Core/WindowManager.h"
#include "Vulkan/Rendering/VulkanRenderer.h"
#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/Panels/Baseline/UISpecSelector.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include "PlayAlong/UIPlayerSelector.h"

struct ActorEvent;
struct EncounterSegment;
struct CombatantInfoEvent;
struct MapChangeEvent;
struct DeathEvent;
struct ResurrectEvent;
struct ExtractedData;
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
class EventDatabase;
class SpellCastDatabase;
class ResourceDatabase;
class CombatDatabase;
class DeathDatabase;
class AuraDatabase;
class DispelDatabase;
class AvoidanceDatabase;
class EmoteDatabase;

namespace warcraftlogs {
    class WarcraftLogsSession;
    class ResponseCache;
}
class UIWarcraftLogsPanel;

class UIManager {
public:
    UIManager(int width, int height, const std::string& title);
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    // Initialize the UI system (GLFW + Vulkan)
    bool initialize();

    // Load event database for visualization
    void loadEventDatabase(const EventDatabase* db);

    // Load spell cast database for GCD history
    void loadSpellCastDatabase(const SpellCastDatabase* db);

    // Load spell GCD data from CSV file
    bool loadSpellGCDData(const std::string& csvPath);

    // Load resource database for health/power tracking
    void loadResourceDatabase(const ResourceDatabase* db);

    // Load combat database for damage/healing metrics
    void loadCombatDatabase(const CombatDatabase* db);

    // Load death database for unit death/resurrection tracking
    void loadDeathDatabase(const DeathDatabase* db);

    // Load aura database for buff/debuff tracking
    void loadAuraDatabase(const AuraDatabase* db);

    // Load dispel database for dispel/interrupt tracking
    void loadDispelDatabase(const DispelDatabase* db);

    // Load avoidance database for dodge/parry/block/miss tracking
    void loadAvoidanceDatabase(const AvoidanceDatabase* db);

    // Load emote database for boss phase rules
    void loadEmoteDatabase(const EmoteDatabase* db);

    // Load combatant info (spec IDs) for role-based rendering
    void loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo);

    // Generate fallback colors for actors without COMBATANT_INFO (NPCs, pets, etc.)
    // Call after loadCombatantInfo() so player class colors are already set
    void generateFallbackColors();

    // Load map change events for worldmap floor textures
    void loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId = 0);

    // Load spell name map for UI display
    void loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap);

    // Load spell data (icon mappings) for a set of spell IDs (lazy loading)
    // Call this after parsing to load only the icons needed for this encounter
    void loadSpellDataForIds(const std::unordered_set<uint32_t>& spellIds);

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

    // Show encounter selector UI and wait for user selection
    // Returns true if a selection was made, false if canceled/closed
    bool showEncounterSelector(const std::vector<EncounterSegment>& encounters);

    // Show encounter selector with refresh support
    // refreshCallback is called when user clicks Refresh button, returns true if new content found
    // The callback should update encounters and re-pass them if new ones are found
    bool showEncounterSelectorWithRefresh(
        const std::vector<EncounterSegment>& encounters,
        size_t totalLines,
        size_t parsedBytes,
        size_t totalBytes,
        std::function<bool()> refreshCallback
    );

    // Get the encounter selection result (call after showEncounterSelector returns true)
    size_t getSelectedEncounterIndex() const;

    // Update window title
    void setWindowTitle(const std::string& title);

    // Main render loop
    void run();

    // Ask which spec, hero talent and content a baseline is for. Runs before
    // any query, so cancelling costs nothing.
    baseline_ui::SpecSelection pickBaselineSpec();

    // Ask which player to build a reference from. Runs before the fight's
    // events are fetched - the roster comes from report metadata, so
    // cancelling costs nothing.
    PlayAlong::PlayerSelection pickBenchmarkPlayer(
        const std::vector<PlayAlong::PlayerInfo>& players);

    // Rule on what each of the reference's spells does. Runs after the events
    // are in and the profile is built. Neither step enters the replay:
    // preparing a reference needs events, not a 3D view.
    void runClassificationReview(
        uint16_t specId,
        const std::unordered_map<uint32_t, std::string>& spellNames);

    // Show the finished comparison. Runs on its own, without the replay:
    // the report card is the answer, and the replay is something the user
    // opens from it if they want to see a finding in context.
    void runBenchmarkReport();

    // The report card, kept open and refreshed as new pulls land.
    //
    // `onPoll` is called once per frame; whatever it returns is shown on the
    // card as a status line, so the user can tell a pull being read from
    // nothing happening. The card rebuilds from the session every frame, so
    // a callback that swaps in a newer comparison needs to do nothing else
    // for the numbers to move.
    //
    // Runs until the user dismisses the card or closes the window.
    void runLiveCompareReport(const std::function<std::string()>& onPoll);

    // Show what a freshly prepared reference contains. Returns true when the
    // user asked to change a ruling, so the caller can reopen the review and
    // re-cut before showing this again.
    bool runMentorPreview(const std::unordered_map<uint32_t, std::string>& spellNames);

    // Run the three prepare steps - spell roles, phases, mob weights - and
    // return true when the user confirmed. All three shape what the extractor
    // produces, so they are settled before a reference is cut rather than
    // discovered afterwards in three different corners of the app.
    bool runPrepareWizard(uint16_t specId,
                          const std::string& playerName,
                          const std::unordered_map<uint32_t, std::string>& spellNames);

    // Get the underlying GLFW window (for single-instance window hook)
    void* getGlfwWindow() const;

    // Navigation control
    bool shouldReturnToEncounterSelector() const;
    bool shouldReturnToLandingPage() const;
    bool shouldRefreshEncounters() const;
    bool shouldLoadPendingFile() const;
    void resetNavigationFlags();
    void resetRefreshFlag();

    // Set callback to check for externally queued files (e.g., from another instance)
    // If callback returns true, the render loop will exit to allow loading the new file
    void setPendingFileChecker(std::function<bool()> checker);

    // Unload databases before switching encounters
    void unloadDatabases();

    // Get auto-annotation trigger manager for direct access (e.g., snapshot import)
    AutoAnnotationTriggerManager* getAutoAnnotationTriggerManager();

    // Callback type for validating WarcraftLogs API credentials
    using ValidateCredentialsCallback = std::function<bool(const std::string&, const std::string&, std::string&)>;

    // What the last package or season-list action did. Lives here rather than
    // on the landing page because that is rebuilt every time the menu opens,
    // so a message set during an action would be gone before it was read.
    void setLandingStatus(const std::string& message, bool isError = false) {
        landingStatus_ = message;
        landingStatusIsError_ = isError;
    }

    // Show landing page and wait for user choice
    // Returns the user's choice (LocalFile, WarcraftLogs, CachedFight, or Exit)
    // validateCredentials: optional callback to validate API credentials before saving
    // responseCache: optional cache for browsing previously loaded WarcraftLogs fights
    // autoOpenWclDialog: if true, automatically opens the WCL URL dialog (e.g., after auth failure)
    LandingPageResult showLandingPage(ValidateCredentialsCallback validateCredentials = nullptr,
                                       warcraftlogs::ResponseCache* responseCache = nullptr,
                                       bool autoOpenWclDialog = false);

    // Show WarcraftLogs fight selector and wait for user selection
    // Returns the selected fight ID, or -1 if cancelled
    int32_t showFightSelector(warcraftlogs::WarcraftLogsSession* session);

    // Show a progress overlay while executing a long-running task
    // Renders the loading UI while the task runs
    // Returns true if the task completed successfully (not cancelled by window close)
    bool showLoadingOverlay(const std::string& title,
                           std::function<bool(std::function<void(const std::string&, float)>)> task);

    // Single-frame progress update (for use with existing progress callbacks)
    void renderProgressFrame(const std::string& message, float progress);

    // Run Play Along mode - practice encounter positioning
    // Shows player selector, then runs PlayAlongVisualizer
    void runPlayAlong(const EventDatabase* eventDb,
                      const SpellCastDatabase* spellCastDb,
                      const CombatDatabase* combatDb,
                      const AuraDatabase* auraDb,
                      const std::vector<CombatantInfoEvent>& combatantInfo,
                      const std::vector<MapChangeEvent>& mapChanges,
                      const std::vector<DeathEvent>& deathEvents,
                      const std::vector<ResurrectEvent>& resurrectEvents,
                      const std::unordered_map<uint32_t, std::string>* spellNameMap,
                      const std::string& fightName);

private:
    std::string landingStatus_;
    bool landingStatusIsError_ = false;

    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<VulkanRenderer> vulkanRenderer;

    // Encounter selection state
    size_t selectedEncounterIndex_;

    // External file checker callback
    std::function<bool()> pendingFileChecker_;
    bool pendingFileFlag_ = false;
};
