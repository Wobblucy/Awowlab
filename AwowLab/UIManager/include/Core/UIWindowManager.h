#pragma once
#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include "UI/UISettings.h"
#include "UI/ActorVisibilitySettings.h"
#include "UI/CameraSettings.h"
#include "UI/NameplateSettings.h"
#include "UI/FacingIndicatorSettings.h"
#include "UI/DebugSettings.h"

class UIPlaybackPanel;
class UIHelpPanel;
class UICreaturesPanel;
class UIActorSelector;
class UISpellHistory;
class UIRaidFramePanel;
class UIEnemyGridPanel;
class UIActorAppearancePanel;
class CustomShapeRegistry;
class UINameplateSettingsPanel;
class UIActorBreakdownPanel;
class UIMeterPanel;  // Multi-instance meter (replaces Combat/Healing/DamageTaken/Unified)
class UIDeathRecapPanel;
class UIDeathsPanel;
class UIAuraDebugPanel;
class UISpellAssignmentPanel;
class UICooldownPanel;
class UIRaidCooldownPanel;
class UIPlayerBuffPanel;
class UIPlayerDebuffPanel;
class ActorAppearanceSettings;
class AuraDatabase;
class DeathDatabase;
class PlaybackController;
class ActorColorGenerator;
class Camera3D;
class SpellIconLoader;
class FrameDataProvider;
class SpellCastDatabase;
class SpellGCDDatabase;
class CombatDatabase;
class UIAutoAnnotationPanel;
class UIAnnotationListPanel;
class BossTimerStrip;
class UIEnemyCastFilterPanel;
class UIMobWeightPanel;
class UIPhaseEditorPanel;
class UIUpcomingBossCasts;
class UISettingsExportPanel;
class UISettingsHub;
class UIRaidNotesPanel;
class ActorAppearanceExporter;
class SpellAssignmentExporter;
class NameplateSettingsExporter;
class FacingIndicatorExporter;
class BossTimerExporter;
class UILayoutExporter;
struct BossTimerSettings;
struct SpellCastDisplay;
struct InterpolatedPosition;
struct HostileCreatureSpells;

// Menu bar action results
enum class MenuAction {
    None,
    ChangeEncounter,
    ReturnToLandingPage,
    ExportSnapshot
};

// Export snapshot request data
struct ExportSnapshotRequest {
    bool active = false;
    int32_t startTimeMs = 0;
    int32_t endTimeMs = 0;
};

// Manages UI window layout and panel orchestration
class UIWindowManager {
public:
    UIWindowManager();
    ~UIWindowManager();

    UIWindowManager(const UIWindowManager&) = delete;
    UIWindowManager& operator=(const UIWindowManager&) = delete;

    // Load/save UI settings
    void loadSettings();
    void saveSettings();

    // Accessors for settings
    ActorVisibilitySettings& getVisibilitySettings() { return visibilitySettings_; }
    const ActorVisibilitySettings& getVisibilitySettings() const { return visibilitySettings_; }
    CameraSettings& getCameraSettings() { return cameraSettings_; }
    const CameraSettings& getCameraSettings() const { return cameraSettings_; }
    NameplateSettings& getNameplateSettings() { return nameplateSettings_; }
    const NameplateSettings& getNameplateSettings() const { return nameplateSettings_; }
    FacingIndicatorSettings& getFacingIndicatorSettings() { return facingIndicatorSettings_; }
    const FacingIndicatorSettings& getFacingIndicatorSettings() const { return facingIndicatorSettings_; }
    DebugSettings& getDebugSettings() { return debugSettings_; }
    const DebugSettings& getDebugSettings() const { return debugSettings_; }
    UISettings& getUISettings() { return uiSettings_; }
    const UISettings& getUISettings() const { return uiSettings_; }
    ActorAppearanceSettings& getAppearanceSettings() { return *appearanceSettings_; }
    const ActorAppearanceSettings& getAppearanceSettings() const { return *appearanceSettings_; }
    std::shared_ptr<ActorAppearanceSettings> getAppearanceSettingsPtr() { return appearanceSettings_; }

    // Set dependencies
    void setPlaybackController(std::shared_ptr<PlaybackController> playback);
    void setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen);
    void setCamera(std::shared_ptr<Camera3D> camera);
    void setCustomShapeRegistry(CustomShapeRegistry* registry, const std::string& customModelsPath);

    // Load database references
    void loadDeathDatabase(const DeathDatabase* db);
    void loadAuraDatabase(const AuraDatabase* db);

    // Database accessors (for debug dumping)
    const AuraDatabase* getAuraDatabase() const { return auraDatabase_; }

    // Load health scaling data for mob size scaling
    void loadHealthScalingData(uint64_t avgPlayerHealth,
                               const std::unordered_map<std::string, uint64_t>& actorMaxHealth);

    // WCL actor id -> creature id (their GUIDs encode the actor id, not the
    // npc id). Lets baked model defaults resolve. Empty for real logs.
    void loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId);

    // Render all UI panels
    void renderPanels(FrameDataProvider* dataProvider,
                      SpellIconLoader* spellIconLoader,
                      const SpellGCDDatabase* gcdDatabase,
                      uint32_t currentTime);

    // Render menu bar (returns action if menu item was clicked)
    MenuAction renderMenuBar();

    // Get actor selector for external use
    UIActorSelector* getActorSelector() { return actorSelector_.get(); }
    const UIActorSelector* getActorSelector() const { return actorSelector_.get(); }

    // Get meter panel for target selection (Taken By view)
    UIMeterPanel* getMeterPanel1() { return meterPanel1_.get(); }
    UIMeterPanel* getMeterPanel2() { return meterPanel2_.get(); }
    UIMeterPanel* getMeterPanel3() { return meterPanel3_.get(); }

    // Get selected actor GUID
    std::optional<std::string> getSelectedActor() const;

    // Update known players list
    void updateKnownPlayers(const std::shared_ptr<ActorColorGenerator>& colorGen,
                            const std::unordered_map<std::string, std::string>& players);

    // Clear spell assignment cache (call when encounter changes)
    void clearSpellAssignmentCache();

    // Set auto-annotation panel (owned by MapVisualizer, rendered by UIWindowManager)
    void setAutoAnnotationPanel(UIAutoAnnotationPanel* panel) { autoAnnotationPanel_ = panel; }

    // Set annotation list panel (owned by MapVisualizer, menu toggle managed by UIWindowManager)
    void setAnnotationListPanel(UIAnnotationListPanel* panel) { annotationListPanel_ = panel; }

    // Boss timer accessors
    BossTimerSettings& getBossTimerSettings();
    BossTimerStrip* getBossTimerStrip() { return bossTimerStrip_.get(); }
    UIEnemyCastFilterPanel* getEnemyCastFilterPanel() { return enemyCastFilterPanel_.get(); }
    UIMobWeightPanel* getMobWeightPanel() { return mobWeightPanel_.get(); }
    UIUpcomingBossCasts* getUpcomingBossCastsPanel() { return upcomingBossCastsPanel_.get(); }

    // Initialize boss timer with pre-built hostile creature spell data
    // (uses same data as Auto Annotations - called after buildHostileCreatureSpells())
    void setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells);

    // Help panel accessor (for keyboard shortcut toggle)
    UIHelpPanel* getHelpPanel() { return helpPanel_.get(); }

    // Raid notes panel accessor (for context menu integration)
    UIRaidNotesPanel* getRaidNotesPanel() { return raidNotesPanel_.get(); }

    // Settings hub accessor
    UISettingsHub* getSettingsHub() { return settingsHub_.get(); }

    // Open appearance panel for a specific actor (from context menu)
    void openAppearancePanelForActor(const std::string& actorGuid);

    // Open spell assignment panel for a specific spec (from context menu)
    void openSpellAssignmentPanelForSpec(uint16_t specId);

    // Export snapshot modal control
    void openExportSnapshotModal(int32_t currentTimeMs, int32_t maxTimeMs);
    const ExportSnapshotRequest& getExportRequest() const { return exportRequest_; }
    void clearExportRequest() { exportRequest_.active = false; }

private:
    // UI Panels
    std::unique_ptr<UIPlaybackPanel> playbackPanel_;
    std::unique_ptr<UICreaturesPanel> creaturesPanel_;
    std::unique_ptr<UIActorSelector> actorSelector_;
    std::unique_ptr<UISpellHistory> spellHistory_;
    std::unique_ptr<UIRaidFramePanel> raidFramePanel_;
    std::unique_ptr<UIEnemyGridPanel> enemyGridPanel_;
    std::unique_ptr<UIActorAppearancePanel> appearancePanel_;
    std::unique_ptr<UINameplateSettingsPanel> nameplateSettingsPanel_;
    std::unique_ptr<UIActorBreakdownPanel> breakdownPanel_;
    std::unique_ptr<UIMeterPanel> meterPanel1_;  // Meter 1 (visible by default)
    std::unique_ptr<UIMeterPanel> meterPanel2_;  // Meter 2
    std::unique_ptr<UIMeterPanel> meterPanel3_;  // Meter 3
    std::unique_ptr<UIDeathRecapPanel> deathRecapPanel_;
    std::unique_ptr<UIDeathsPanel> deathsPanel_;
    std::unique_ptr<UIAuraDebugPanel> auraDebugPanel_;
    std::unique_ptr<UISpellAssignmentPanel> spellAssignmentPanel_;
    std::unique_ptr<UICooldownPanel> cooldownPanel_;
    std::unique_ptr<UIRaidCooldownPanel> raidCooldownPanel1_;  // Major CDs
    std::unique_ptr<UIRaidCooldownPanel> raidCooldownPanel2_;  // Minor CDs
    std::unique_ptr<UIRaidCooldownPanel> raidCooldownPanel3_;  // Utility CDs
    std::unique_ptr<UIPlayerBuffPanel> playerBuffPanel_;
    std::unique_ptr<UIPlayerDebuffPanel> playerDebuffPanel_;
    std::unique_ptr<UIHelpPanel> helpPanel_;

    // Boss timer components
    std::unique_ptr<BossTimerStrip> bossTimerStrip_;
    std::unique_ptr<UIEnemyCastFilterPanel> enemyCastFilterPanel_;
    std::unique_ptr<UIUpcomingBossCasts> upcomingBossCastsPanel_;
    std::unique_ptr<BossTimerSettings> bossTimerSettings_;

    // Settings export panel
    std::unique_ptr<UISettingsExportPanel> settingsExportPanel_;

    // Raid notes panel
    std::unique_ptr<UIRaidNotesPanel> raidNotesPanel_;

    // Settings hub
    std::unique_ptr<UISettingsHub> settingsHub_;

    // Mob weighting panel
    std::unique_ptr<UIMobWeightPanel> mobWeightPanel_;

    // Boss phase editor panel
    std::unique_ptr<UIPhaseEditorPanel> phaseEditorPanel_;

    // Database references
    const DeathDatabase* deathDatabase_;
    const AuraDatabase* auraDatabase_;

    // External panels (not owned)
    UIAutoAnnotationPanel* autoAnnotationPanel_ = nullptr;
    UIAnnotationListPanel* annotationListPanel_ = nullptr;

    // Dependencies
    std::shared_ptr<PlaybackController> playbackController_;
    std::shared_ptr<ActorColorGenerator> colorGenerator_;
    std::shared_ptr<Camera3D> camera3D_;

    // Settings
    ActorVisibilitySettings visibilitySettings_;
    CameraSettings cameraSettings_;
    NameplateSettings nameplateSettings_;
    FacingIndicatorSettings facingIndicatorSettings_;
    DebugSettings debugSettings_;
    UISettings uiSettings_;
    std::shared_ptr<ActorAppearanceSettings> appearanceSettings_;

    // Warning popup state
    bool warningPopupOpen_ = false;

    // Export snapshot modal state
    bool showExportModal_ = false;
    int32_t exportStartMs_ = 0;
    int32_t exportEndMs_ = 0;
    int32_t exportMaxTimeMs_ = 0;
    ExportSnapshotRequest exportRequest_;

    // Helper functions
    void applyWindowState(const char* windowName, WindowState& state);
    void renderWarningPopup();
    void renderExportSnapshotModal();
    void registerSettingsExporters();

    // Rebuild the boss phase windows for the current encounter from
    // PhaseSettings rules (first casts, elapsed times, emotes) and
    // push them to all three meter panels. Called when an encounter
    // loads and after a phase rule is added or removed.
    void rebuildPhases(FrameDataProvider* dataProvider);

    // Track when a new encounter (or a fresh pull) comes in so phases
    // are rebuilt exactly once per load
    uint32_t phaseEncounterId_ = 0;
    const SpellCastDatabase* phaseSpellCastDb_ = nullptr;

    // Settings export adapters (keep alive for duration of UIWindowManager)
    std::unique_ptr<ActorAppearanceExporter> actorAppearanceExporter_;
    std::unique_ptr<SpellAssignmentExporter> spellAssignmentExporter_;
    std::unique_ptr<NameplateSettingsExporter> nameplateSettingsExporter_;
    std::unique_ptr<FacingIndicatorExporter> facingIndicatorExporter_;
    std::unique_ptr<BossTimerExporter> bossTimerExporter_;
    std::unique_ptr<UILayoutExporter> uiLayoutExporter_;

    // Sections of renderPanels()
    void renderNavigationPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader);
    void renderFramePanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader, uint32_t currentTime);
    void renderCombatPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader, uint32_t currentTime);
    void renderCooldownPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader,
                              const SpellGCDDatabase* gcdDatabase, uint32_t currentTime);
    void renderDebugPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader, uint32_t currentTime);
    void renderModalPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader, uint32_t currentTime);
};
