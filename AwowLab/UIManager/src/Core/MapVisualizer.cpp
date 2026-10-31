#include "Core/MapVisualizer.h"
#include "Core/FrameDataProvider.h"
#include "Core/RenderingCoordinator.h"
#include "Core/UIWindowManager.h"
#include "Core/HostileSpellCollector.h"
#include "Geometry/CustomShapeRegistry.h"
#include "Vulkan/Textures/VulkanTextureManager.h"
#include "UI/SpellIconLoader.h"  // For SpellIconLoader -> ISpellIconRenderer conversion
#include "Core/MapChangeController.h"
#include "UI/EncounterTextureManager.h"
#include "UI/UIActorSelector.h"
#include "UI/Panels/Navigation/UIHelpPanel.h"
#include "UI/Panels/Combat/UIMeterPanel.h"
#include "Color/ActorColorGenerator.h"
#include "Playback/PlaybackController.h"
#include "Camera/Camera3D.h"
#include "Animation/ActorPositionInterpolator.h"
#include "Camera/CameraController.h"
#include "Input/MouseRaycaster.h"
#include "../../Database/EventDatabase.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/ResourceDatabase.h"
#include "../../Database/CombatDatabase.h"
#include "../../Database/DeathDatabase.h"
#include "../../Database/AuraDatabase.h"
#include "../../Database/AvoidanceDatabase.h"
#include "../../Database/OutputWriter.h"
#ifndef NDEBUG
#include "../../Database/DebugDatabaseDumper.h"
#endif
#include "AnnotationManager/include/AnnotationManager.h"
#include "AnnotationManager/include/UIAnnotationContextMenu.h"
#include "AnnotationManager/include/UIAnnotationPropertiesPanel.h"
#include "AnnotationManager/include/UIAnnotationListPanel.h"
#include "AnnotationManager/include/AutoAnnotationTriggerManager.h"
#include "AnnotationManager/include/UIAutoAnnotationPanel.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "UI/SpellGCDDatabase.h"
#include "UI/Panels/Navigation/BossTimerStrip.h"
#include "UI/Panels/Navigation/UIUpcomingBossCasts.h"
#include "Core/FrameProfiler.h"
#include "Core/UnifiedSettings.h"
#include "Core/Utils/ExecutablePath.h"
#include "Core/Benchmark/BenchmarkSession.h"
#include "Core/CurrentEncounter.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/AwlUI/Theme.h"
#include "Core/LocalizationManager.h"
#include "Core/ErrorLogger.h"
#include <imgui.h>
#include <cmath>
#include <unordered_set>

namespace {
    // Screen-space distance threshold for actor selection (in pixels)
    constexpr float kScreenProximityThreshold = 100.0f;
    constexpr float kScreenProximityThresholdSq = kScreenProximityThreshold * kScreenProximityThreshold;

    constexpr float kMouseRotateSensitivity = 0.005f;   // radians per pixel, right drag
    constexpr float kMiddleDragPanSensitivity = 0.005f;
    constexpr float kWheelZoomStep = 5.0f;
    constexpr int32_t kKeyboardSkipMs = 5000;           // Q / E

    float distanceBetween(const ImVec2& a, const ImVec2& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }
}

MapVisualizer::MapVisualizer()
    : frameDataProvider_(std::make_unique<FrameDataProvider>())
    , renderingCoordinator_(std::make_unique<RenderingCoordinator>())
    , uiWindowManager_(std::make_unique<UIWindowManager>())
    , colorGenerator_(std::make_shared<ActorColorGenerator>())
    , playbackController_(std::make_shared<PlaybackController>())
    , camera3D_(std::make_shared<Camera3D>())
    , positionInterpolator_(std::make_shared<ActorPositionInterpolator>())
    , cameraController_(std::make_shared<CameraController>())
    , mouseRaycaster_(std::make_unique<MouseRaycaster>())
    , annotationManager_(std::make_unique<AnnotationManager>())
    , annotationContextMenu_(std::make_unique<UIAnnotationContextMenu>())
    , annotationPropertiesPanel_(std::make_unique<UIAnnotationPropertiesPanel>())
    , annotationListPanel_(std::make_unique<UIAnnotationListPanel>())
    , autoAnnotationTriggerManager_(std::make_unique<AutoAnnotationTriggerManager>())
    , autoAnnotationPanel_(std::make_unique<UIAutoAnnotationPanel>())
    , mapChangeController_(std::make_unique<MapChangeController>())
    , rightClickStartPos_(0.0f, 0.0f)
    , lastFrameTime_(-1.0)
    , shouldReturnToEncounterSelector_(false)
    , shouldReturnToLandingPage_(false)
    , shouldRefreshEncounters_(false)
{
    frameDataProvider_->setPlaybackController(playbackController_);
    frameDataProvider_->setPositionInterpolator(positionInterpolator_);
    frameDataProvider_->setColorGenerator(colorGenerator_);
    frameDataProvider_->setActorSelector(uiWindowManager_->getActorSelector());

    renderingCoordinator_->setCamera(camera3D_);
    renderingCoordinator_->setCameraController(cameraController_);
    renderingCoordinator_->setColorGenerator(colorGenerator_);
    renderingCoordinator_->setAppearanceSettings(uiWindowManager_->getAppearanceSettingsPtr());
    renderingCoordinator_->setAnnotationManager(annotationManager_.get());

    uiWindowManager_->setPlaybackController(playbackController_);
    uiWindowManager_->setColorGenerator(colorGenerator_);
    uiWindowManager_->setCamera(camera3D_);
    uiWindowManager_->loadSettings();

    mapChangeController_->setRenderingCoordinator(renderingCoordinator_.get());
    mapChangeController_->setFrameDataProvider(frameDataProvider_.get());
    mapChangeController_->setPlaybackController(playbackController_.get());

    playbackController_->setResetCallback([this]() {
        positionInterpolator_->reset();
        // Clear active auto-annotations when timeline resets (cache stays valid)
        if (autoAnnotationTriggerManager_) {
            autoAnnotationTriggerManager_->clearAutoAnnotations(annotationManager_.get());
        }
    });

    autoAnnotationPanel_->setTriggerManager(autoAnnotationTriggerManager_.get());
    autoAnnotationPanel_->setSpellIconLoader(renderingCoordinator_->getSpellIconLoader());

    uiWindowManager_->setAnnotationListPanel(annotationListPanel_.get());
    uiWindowManager_->setAutoAnnotationPanel(autoAnnotationPanel_.get());

    // When triggers are modified (added/updated/removed), invalidate cache to force rebuild
    autoAnnotationTriggerManager_->setChangeCallback([this]() {
        if (autoAnnotationTriggerManager_ && annotationManager_) {
            autoAnnotationTriggerManager_->clearAutoAnnotations(annotationManager_.get());
        }
    });

    // Auto-load spell GCD data from binary data file (version selected in settings)
    std::string spellDataVersion = SettingsCache::instance().get().spellDataVersion;
    std::string binPath = awow::getExecutableDirectory() + "data\\spell_data_" + spellDataVersion + ".bin";
    loadSpellGCDData(binPath);
}

MapVisualizer::~MapVisualizer() {
    uiWindowManager_->saveSettings();
}

bool MapVisualizer::initialize3D(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                                  VkQueue graphicsQueue, VkCommandPool commandPool) {
    if (!renderingCoordinator_->initialize3D(device, physicalDevice, renderPass, graphicsQueue, commandPool)) {
        return false;
    }

    // The texture manager for MTL files is attached later, in initializeSpellIcons
    std::string customModelsPath = awow::getExecutableDirectory() + "custom_models";
    auto customShapeRegistry = std::make_shared<CustomShapeRegistry>(device, physicalDevice);
    customShapeRegistry->loadRotations();
    customShapeRegistry->scanDirectory(customModelsPath);  // Models load lazily

    // Baked season model pack: pre-decimated creature models in the GPU wire
    // format. getShape() resolves these instantly (memcpy -> GPU) with no OBJ
    // parsing. One pack ships per game version; if several are present the
    // last one loaded wins.
    {
        std::error_code ec;
        std::filesystem::path dataDir = awow::getExecutableDirectory() + "data";
        std::filesystem::directory_iterator dit(dataDir, ec), dend;
        for (; !ec && dit != dend; dit.increment(ec)) {
            if (dit->path().extension() == ".awmp")
                customShapeRegistry->loadBakedPack(dit->path().string());
        }
    }

    // A directory refresh destroys shape textures; drop the descriptor sets
    // that were built for them
    customShapeRegistry->setShapesReplacedCallback([this]() {
        renderingCoordinator_->clearTexturedModelDescriptorCache();
    });

    renderingCoordinator_->setCustomShapeRegistry(customShapeRegistry);

    uiWindowManager_->setCustomShapeRegistry(customShapeRegistry.get(), customModelsPath);

    return true;
}

bool MapVisualizer::initializeSpellIcons(VkDevice device, VkPhysicalDevice physicalDevice,
                                          VkQueue graphicsQueue, VkCommandPool commandPool,
                                          VkDescriptorPool descriptorPool) {
    // This must happen after initialize3D which creates the registry
    auto* customRegistry = renderingCoordinator_->getCustomShapeRegistry();
    if (customRegistry) {
        auto customModelTextureManager = std::make_shared<VulkanTextureManager>();
        if (customModelTextureManager->initialize(device, physicalDevice, graphicsQueue,
                                                   commandPool, descriptorPool)) {
            customRegistry->setTextureManager(customModelTextureManager);
            customModelTextureManager_ = std::move(customModelTextureManager);
        }
    }

    encounterTextureManager_ = std::make_unique<EncounterTextureManager>();
    if (encounterTextureManager_->initialize(device, physicalDevice, graphicsQueue, commandPool, descriptorPool)) {
        mapChangeController_->setEncounterTextureManager(encounterTextureManager_.get());
    } else {
        AWOW_LOG_WARNING("Rendering", "Encounter texture manager failed to initialize; per-encounter floor textures are disabled");
    }

    return renderingCoordinator_->initializeSpellIcons(device, physicalDevice, graphicsQueue,
                                                        commandPool, descriptorPool);
}

void MapVisualizer::cleanup3D(VkDevice device) {
    if (auto* customRegistry = renderingCoordinator_->getCustomShapeRegistry()) {
        customRegistry->cleanup(device);
    }
    if (customModelTextureManager_) {
        customModelTextureManager_->flushDeletionQueue(true);
        customModelTextureManager_.reset();
    }

    if (encounterTextureManager_) {
        encounterTextureManager_->cleanup();
    }
    renderingCoordinator_->cleanup3D(device);
}

void MapVisualizer::loadEventDatabase(const EventDatabase* db) {
    frameDataProvider_->loadEventDatabase(db);

    // The time spent in menus or loading isn't a frame delta
    lastFrameTime_ = -1.0;

    // Clear loaded custom models on encounter swap (lazy reload as needed)
    if (auto* registry = renderingCoordinator_->getCustomShapeRegistry()) {
        registry->clearLoadedModels();
    }

    updateMinimapBounds();
    centerCameraOnEncounter();

    // Set initial map from first event's mapId (handles cases where MAP_CHANGE doesn't fire)
    if (db && !db->empty()) {
        uint16_t initialMapId = db->getFirstMapId();
        float firstX = db->getFirstX();
        float firstY = db->getFirstY();
        mapChangeController_->setInitialMap(initialMapId, firstX, firstY);
    }
}

void MapVisualizer::updateMinimapBounds() {
    if (!frameDataProvider_->hasBounds()) {
        return;
    }
    renderingCoordinator_->setMinimapBounds(
        static_cast<int32_t>(frameDataProvider_->getMinX()), static_cast<int32_t>(frameDataProvider_->getMaxX()),
        static_cast<int32_t>(frameDataProvider_->getMinY()), static_cast<int32_t>(frameDataProvider_->getMaxY()));
}

void MapVisualizer::centerCameraOnEncounter() {
    if (!frameDataProvider_->hasBounds()) {
        return;
    }
    float centerX = (frameDataProvider_->getMinX() + frameDataProvider_->getMaxX()) / 2.0f;
    float centerY = (frameDataProvider_->getMinY() + frameDataProvider_->getMaxY()) / 2.0f;
    camera3D_->setTarget(centerX, centerY, 0.0f);
}

void MapVisualizer::loadSpellCastDatabase(const SpellCastDatabase* db) {
    frameDataProvider_->loadSpellCastDatabase(db);

    if (db && !db->empty()) {
        renderingCoordinator_->preloadSpellIcons(db->getUniqueSpellIDs());
    }
}

bool MapVisualizer::loadSpellGCDData(const std::string& path) {
    spellGCDDatabase_ = std::make_unique<SpellGCDDatabase>();
    // Try binary format first (preferred), fall back to CSV
    if (path.ends_with(".bin")) {
        if (spellGCDDatabase_->loadFromBinary(path)) {
            return true;
        }
    } else if (spellGCDDatabase_->loadFromCSV(path)) {
        return true;
    }
    AWOW_LOG_WARNING("Data", "Failed to load spell GCD data from " + path + "; GCD display falls back to 1.5s");
    return false;
}

void MapVisualizer::loadResourceDatabase(const ResourceDatabase* db) {
    frameDataProvider_->loadResourceDatabase(db);
}

void MapVisualizer::loadCombatDatabase(const CombatDatabase* db) {
    frameDataProvider_->loadCombatDatabase(db);
}

void MapVisualizer::loadDeathDatabase(const DeathDatabase* db) {
    frameDataProvider_->loadDeathDatabase(db);
    uiWindowManager_->loadDeathDatabase(db);
}

void MapVisualizer::loadAuraDatabase(const AuraDatabase* db) {
    uiWindowManager_->loadAuraDatabase(db);
    frameDataProvider_->loadAuraDatabase(db);

    // Needs both SpellCastDatabase and AuraDatabase, so it runs here
    buildHostileCreatureSpells();

    if (autoAnnotationTriggerManager_) {
        autoAnnotationTriggerManager_->setSpellNameMap(&spellNameMap_);
        autoAnnotationTriggerManager_->loadFromFile();
    }
}

void MapVisualizer::loadDispelDatabase(const DispelDatabase* db) {
    frameDataProvider_->loadDispelDatabase(db);
}

void MapVisualizer::loadAvoidanceDatabase(const AvoidanceDatabase* db) {
    frameDataProvider_->loadAvoidanceDatabase(db);
}

void MapVisualizer::loadEmoteDatabase(const EmoteDatabase* db) {
    frameDataProvider_->loadEmoteDatabase(db);
}

void MapVisualizer::loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo) {
    // Only spec IDs are cached. The generator applies class colors at lookup
    // time from the cached spec, so the class-colors setting can switch
    // between class and generated colors without a reload.
    for (const auto& info : combatantInfo) {
        if (!info.player_guid.empty() && info.spec_id > 0) {
            colorGenerator_->cacheSpecId(info.player_guid, info.spec_id);
        }
    }
}

void MapVisualizer::generateFallbackColors() {
    // Call after loadCombatantInfo() so player class colors are already set
    frameDataProvider_->generateFallbackColors();
}

void MapVisualizer::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId) {
    mapChangeController_->setCurrentEncounterId(encounterId);
    mapChangeController_->loadMapChangeEvents(mapChanges);
}

void MapVisualizer::loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap) {
    spellNameMap_ = spellNameMap;
    frameDataProvider_->loadSpellNameMap(&spellNameMap_);
}

void MapVisualizer::loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap) {
    guidToNameMap_ = guidToNameMap;
    frameDataProvider_->loadGuidToNameMap(&guidToNameMap_);
}

void MapVisualizer::loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    uiWindowManager_->loadActorNpcIdMap(actorIdToNpcId);
}

void MapVisualizer::loadHealthScalingData(uint64_t avgPlayerHealth,
                                           const std::unordered_map<std::string, uint64_t>& actorMaxHealth) {
    uiWindowManager_->loadHealthScalingData(avgPlayerHealth, actorMaxHealth);
}

void MapVisualizer::setExtractedData(const ExtractedData* data) {
    extractedData_ = data;
}

void MapVisualizer::setExportCallback(ExportSnapshotCallback callback) {
    exportCallback_ = std::move(callback);
}

void MapVisualizer::buildHostileCreatureSpells() {
    HostileSpellCollector collector;
    hostileCreatureSpells_ = collector.collect(
        frameDataProvider_->getSpellCastDatabase(),
        uiWindowManager_->getAuraDatabase(),
        frameDataProvider_->getCombatDatabase(),
        frameDataProvider_->getEventDatabase(),
        colorGenerator_.get()
    );

    autoAnnotationPanel_->setHostileCreatureSpells(hostileCreatureSpells_);
    autoAnnotationPanel_->setAnnotationManager(annotationManager_.get());
    autoAnnotationPanel_->setDatabases(
        frameDataProvider_->getSpellCastDatabase(),
        frameDataProvider_->getCombatDatabase(),
        uiWindowManager_->getAuraDatabase(),
        frameDataProvider_->getEventDatabase()
    );
    uiWindowManager_->setHostileCreatureSpells(hostileCreatureSpells_);

    if (auto* bossTimer = uiWindowManager_->getBossTimerStrip()) {
        bossTimer->setDatabases(
            frameDataProvider_->getSpellCastDatabase(),
            uiWindowManager_->getAuraDatabase(),
            frameDataProvider_->getCombatDatabase(),
            colorGenerator_.get()
        );
    }

    if (auto* upcomingPanel = uiWindowManager_->getUpcomingBossCastsPanel()) {
        upcomingPanel->setDependencies(
            renderingCoordinator_->getSpellIconLoader(),
            frameDataProvider_->getSpellCastDatabase(),
            &uiWindowManager_->getBossTimerSettings(),
            colorGenerator_.get()
        );
    }
}

// Playback control delegations
void MapVisualizer::setTimeRange(int32_t start_ms, int32_t end_ms) {
    playbackController_->loadTimeRange(start_ms, end_ms);
}

void MapVisualizer::setCurrentTime(int32_t time_ms) {
    playbackController_->setCurrentTime(time_ms);
}

int32_t MapVisualizer::getCurrentTime() const {
    return playbackController_->getCurrentTime();
}

int32_t MapVisualizer::getMaxTime() const {
    return playbackController_->getMaxTime();
}

void MapVisualizer::play() { playbackController_->play(); }
void MapVisualizer::pause() { playbackController_->pause(); }
void MapVisualizer::togglePlayPause() { playbackController_->togglePlayPause(); }
bool MapVisualizer::isPlaybackActive() const { return playbackController_->isPlaying(); }
void MapVisualizer::setPlaybackSpeed(float speed) { playbackController_->setSpeed(speed); }

bool MapVisualizer::shouldReturnToEncounterSelector() const {
    return shouldReturnToEncounterSelector_;
}

bool MapVisualizer::shouldReturnToLandingPage() const {
    return shouldReturnToLandingPage_;
}

bool MapVisualizer::shouldRefreshEncounters() const {
    return shouldRefreshEncounters_;
}

void MapVisualizer::resetNavigationFlags() {
    shouldReturnToEncounterSelector_ = false;
    shouldReturnToLandingPage_ = false;
    shouldRefreshEncounters_ = false;
}

void MapVisualizer::resetRefreshFlag() {
    shouldRefreshEncounters_ = false;
}

void MapVisualizer::unloadDatabases() {
    // Clear database pointers before the databases go out of scope
    frameDataProvider_->loadEventDatabase(nullptr);
    frameDataProvider_->loadSpellCastDatabase(nullptr);
    frameDataProvider_->loadResourceDatabase(nullptr);
    frameDataProvider_->loadCombatDatabase(nullptr);
    frameDataProvider_->loadDeathDatabase(nullptr);
    frameDataProvider_->loadAuraDatabase(nullptr);
    frameDataProvider_->loadDispelDatabase(nullptr);
    frameDataProvider_->loadAvoidanceDatabase(nullptr);
    frameDataProvider_->loadEmoteDatabase(nullptr);

    uiWindowManager_->clearSpellAssignmentCache();

    if (autoAnnotationTriggerManager_) {
        autoAnnotationTriggerManager_->saveToFile();
        autoAnnotationTriggerManager_->clearAutoAnnotations(annotationManager_.get());  // Delete annotations before clearing cache
        autoAnnotationTriggerManager_->clearCache();  // Now safe to clear tracking map
        autoAnnotationTriggerManager_->clearTriggers();
    }

    if (annotationManager_) {
        annotationManager_->saveForEncounter();
        annotationManager_->clearCurrentEncounter();
    }

    // Clear cached textured model descriptor sets to prevent stale texture references
    if (renderingCoordinator_) {
        renderingCoordinator_->clearTexturedModelDescriptorCache();
    }

    mapChangeController_->clear();
}

SpellIconLoader* MapVisualizer::getSpellIconLoader() {
    return renderingCoordinator_ ? renderingCoordinator_->getSpellIconLoader() : nullptr;
}

AutoAnnotationTriggerManager* MapVisualizer::getAutoAnnotationTriggerManager() {
    return autoAnnotationTriggerManager_.get();
}

void MapVisualizer::processTextureCleanup() {
    if (renderingCoordinator_) {
        renderingCoordinator_->processTextureCleanup();
    }
    if (encounterTextureManager_) {
        encounterTextureManager_->processTextureCleanup();
    }
    if (customModelTextureManager_) {
        customModelTextureManager_->processDeletionQueue();
    }
}

std::string MapVisualizer::getSelectedActorGuid() const {
    auto selected = uiWindowManager_->getSelectedActor();
    return selected.value_or("");
}

void MapVisualizer::render3D(VkDevice device, VkCommandBuffer commandBuffer, VkExtent2D extent) {
    FRAME_PROFILE_BEGIN_FRAME();

    cachedExtent_ = extent;

    if (!renderingCoordinator_->isInitialized() || !frameDataProvider_->hasEvents()) {
        return;
    }

    {
        FRAME_PROFILE_SCOPE("FrameData::updateFrame");
        frameDataProvider_->updateFrame();
    }
    if (!frameDataProvider_->hasPositions()) {
        return;
    }

    {
        FRAME_PROFILE_SCOPE("FrameData::checkMapChange");
        frameDataProvider_->checkMapChange();
    }
    updateMinimapBounds();

    // Update worldmap floor texture based on actor position map_id (checked every 0.5 seconds)
    {
        FRAME_PROFILE_SCOPE("updateMapFromActorPositions");
        mapChangeController_->updateMapFromActorPositions();
    }

    if (mouseRaycaster_ && camera3D_) {
        FRAME_PROFILE_SCOPE("MouseRaycaster::update");
        float viewMatrix[16], projMatrix[16];
        camera3D_->getViewMatrix(viewMatrix);
        camera3D_->getProjectionMatrix(projMatrix, static_cast<float>(extent.width) / static_cast<float>(extent.height));
        mouseRaycaster_->updateMatrices(viewMatrix, projMatrix);
        mouseRaycaster_->setViewport(static_cast<float>(extent.width), static_cast<float>(extent.height));
    }

    // Use ImGui's delta time for frame-rate-independent camera smoothing
    float deltaTime = ImGui::GetIO().DeltaTime;

    {
        FRAME_PROFILE_SCOPE("RenderCoord::render3D");
        renderingCoordinator_->render3D(device, commandBuffer, extent,
                                         frameDataProvider_.get(),
                                         uiWindowManager_->getVisibilitySettings(),
                                         uiWindowManager_->getCameraSettings(),
                                         uiWindowManager_->getNameplateSettings(),
                                         uiWindowManager_->getFacingIndicatorSettings(),
                                         getSelectedActorGuid(),
                                         deltaTime);
    }
}

void MapVisualizer::render() {
    double currentFrameTime = ImGui::GetTime();
    if (lastFrameTime_ < 0.0) {
        // First frame: initialize without updating (no valid delta yet)
        lastFrameTime_ = currentFrameTime;
    } else {
        playbackController_->update(currentFrameTime - lastFrameTime_);
        lastFrameTime_ = currentFrameTime;
    }

    if (auto* registry = renderingCoordinator_->getCustomShapeRegistry()) {
        registry->processLoadQueue();
    }

    int32_t currentTime = playbackController_->getCurrentTime();
    if (autoAnnotationTriggerManager_ && autoAnnotationTriggerManager_->hasTriggers()) {
        if (!autoAnnotationTriggerManager_->isCacheValid()) {
            autoAnnotationTriggerManager_->buildCache(
                frameDataProvider_->getSpellCastDatabase(),
                frameDataProvider_->getCombatDatabase(),
                uiWindowManager_->getAuraDatabase(),
                frameDataProvider_->getEventDatabase(),
                frameDataProvider_->getDeathDatabase()
            );
        }

        // Cache has pre-computed positions, so scrubbing works correctly
        autoAnnotationTriggerManager_->updateAnnotations(
            currentTime,
            annotationManager_.get()
        );
    }

    // Preload upcoming map textures (10s lookahead, cheap if already cached)
    mapChangeController_->preloadUpcomingMapTextures();

    {
        FRAME_PROFILE_SCOPE("FrameData::updateFrame2");
        frameDataProvider_->updateFrame();
    }

    if (annotationManager_ && frameDataProvider_ && frameDataProvider_->hasPositions()) {
        annotationManager_->updatePositions(frameDataProvider_->getActorPositions(),
                                            playbackController_->getCurrentTime());
    }

    {
        FRAME_PROFILE_SCOPE("UI::renderMenuBar");
        MenuAction menuAction = uiWindowManager_->renderMenuBar();
        if (menuAction == MenuAction::ChangeEncounter) {
            shouldReturnToEncounterSelector_ = true;
        } else if (menuAction == MenuAction::ReturnToLandingPage) {
            shouldReturnToLandingPage_ = true;
        }
    }


    {
        auto exportRequest = uiWindowManager_->getExportRequest();
        if (exportRequest.active && exportCallback_) {
            uiWindowManager_->clearExportRequest();

            std::vector<Annotation> annotations;
            std::vector<AutoAnnotationTrigger> triggers;
            std::vector<CachedAnnotationEvent> cachedEvents;

            if (annotationManager_) {
                annotations = annotationManager_->getAnnotations();
            }
            if (autoAnnotationTriggerManager_) {
                triggers = autoAnnotationTriggerManager_->getTriggers();
                cachedEvents = autoAnnotationTriggerManager_->getCachedEvents();
            }

            exportCallback_(extractedData_, annotations, triggers, cachedEvents,
                           exportRequest.startTimeMs, exportRequest.endTimeMs);
        } else if (exportRequest.active) {
            uiWindowManager_->clearExportRequest();
        }
    }

    if (uiWindowManager_->getCameraSettings().resetRequested) {
        uiWindowManager_->getCameraSettings().resetRequested = false;
        centerCameraOnEncounter();
    }

    {
        FRAME_PROFILE_SCOPE("UI::renderPanels");
        uiWindowManager_->renderPanels(frameDataProvider_.get(),
                                        renderingCoordinator_->getSpellIconLoader(),
                                        spellGCDDatabase_.get(),
                                        playbackController_->getCurrentTime());
    }

#ifndef NDEBUG
    if (uiWindowManager_->getDebugSettings().dumpDatabasesRequested) {
        uiWindowManager_->getDebugSettings().dumpDatabasesRequested = false;
        DebugDatabaseDumper::dumpAll(
            ".",
            "ui_",  // Manual UI dump (source unknown at this point)
            frameDataProvider_->getEventDatabase(),
            frameDataProvider_->getSpellCastDatabase(),
            frameDataProvider_->getResourceDatabase(),
            frameDataProvider_->getCombatDatabase(),
            frameDataProvider_->getDeathDatabase(),
            uiWindowManager_->getAuraDatabase(),
            nullptr,  // ActorMap not available here
            nullptr,  // combatantInfo
            nullptr,  // mapChanges
            nullptr,  // summons
            nullptr,  // resurrects
            autoAnnotationTriggerManager_.get(),  // annotation triggers (owned by MapVisualizer)
            &spellNameMap_,   // spell names (owned by MapVisualizer)
            &guidToNameMap_,  // GUID to name map (owned by MapVisualizer)
            nullptr,  // auraEventsRaw - not available
            nullptr   // extractedData - not available
        );
    }
#endif

    if (uiWindowManager_->getUISettings().minimapWindow.visible) {
        FRAME_PROFILE_SCOPE("UI::renderMinimap");
        renderMinimapWindow();
    }

    {
        FRAME_PROFILE_SCOPE("handleCameraInput");
        handleCameraInput();
    }

    {
        FRAME_PROFILE_SCOPE("renderNameplateOverlay");
        renderingCoordinator_->renderNameplateOverlay(uiWindowManager_->getNameplateSettings(),
                                                       frameDataProvider_.get());
    }

    {
        FRAME_PROFILE_SCOPE("renderFacingIndicator");
        renderingCoordinator_->renderFacingIndicator(uiWindowManager_->getFacingIndicatorSettings(),
                                                      frameDataProvider_.get(),
                                                      getSelectedActorGuid(),
                                                      cachedExtent_);
    }

    FRAME_PROFILE_RENDER_OVERLAY();

#ifndef NDEBUG
    // Floor coordinates under the mouse cursor (debug builds only)
    ImGuiIO& io = ImGui::GetIO();
    if (mouseRaycaster_ && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        auto hit = mouseRaycaster_->raycastToFloor(io.MousePos.x, io.MousePos.y, -1.0f);
        if (hit.has_value()) {
            ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.7f);
            if (ImGui::Begin("##FloorCoords", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Floor X: %.2f", hit->worldX);
                ImGui::Text("Floor Y: %.2f", hit->worldY);
            }
            ImGui::End();
        }
    }
#endif
}

void MapVisualizer::renderMinimapWindow() {
    auto& windowState = uiWindowManager_->getUISettings().minimapWindow;
    if (!windowState.visible) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (windowState.firstFrame) {
        ImVec2 absolutePos = windowState.getAbsolutePosition(viewport);
        ImGui::SetNextWindowPos(absolutePos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(windowState.size, ImGuiCond_FirstUseEver);
        windowState.firstFrame = false;
    }

    if (ImGui::Begin("2D Minimap")) {
        ImVec2 currentPos = ImGui::GetWindowPos();
        ImVec2 currentSize = ImGui::GetWindowSize();

        windowState.size = currentSize;
        windowState.updateFromAbsolutePosition(currentPos, viewport);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.x < 50.0f) canvasSize.x = 50.0f;
        if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 20, 20, 255));

        renderingCoordinator_->renderMinimap(drawList, canvasPos, canvasSize,
                                              frameDataProvider_.get(),
                                              uiWindowManager_->getVisibilitySettings(),
                                              uiWindowManager_->getNameplateSettings(),
                                              getSelectedActorGuid());

        ImGui::InvisibleButton("minimap_canvas", canvasSize);
        renderingCoordinator_->handleMinimapInput(canvasSize);
    }
    ImGui::End();
}

void MapVisualizer::handleCameraInput() {
    renderAnnotationPanels();

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    handleMouseInput();

    // Typing into a text field mustn't drive playback. WantTextInput rather
    // than WantCaptureKeyboard: the latter stays set whenever any panel has
    // focus, so one click on a panel would turn the view's hotkeys off.
    if (io.WantTextInput) return;

    handleKeyboardShortcuts();
}

void MapVisualizer::renderAnnotationPanels() {
    if (annotationContextMenu_) {
        int32_t currentTime = getCurrentTime();
        uint64_t createdId = annotationContextMenu_->render(annotationManager_.get(), currentTime);
        if (createdId != 0) {
            annotationManager_->setSelectedAnnotation(createdId);
            if (annotationPropertiesPanel_) {
                annotationPropertiesPanel_->setVisible(true);
            }
            if (annotationListPanel_) {
                annotationListPanel_->setVisible(true);
            }
        }

        auto navRequest = UIAnnotationContextMenu::consumePendingNavigation();
        if (navRequest.target == UIAnnotationContextMenu::ActorNavigationRequest::Target::AppearancePanel) {
            uiWindowManager_->openAppearancePanelForActor(navRequest.actorGuid);
        } else if (navRequest.target == UIAnnotationContextMenu::ActorNavigationRequest::Target::SpellSettings) {
            uiWindowManager_->openSpellAssignmentPanelForSpec(navRequest.specId);
        }
    }

    if (annotationPropertiesPanel_ && annotationPropertiesPanel_->isVisible()) {
        int32_t currentTime = getCurrentTime();
        annotationPropertiesPanel_->render(annotationManager_.get(), currentTime);
    }

    if (annotationListPanel_ && annotationListPanel_->isVisible()) {
        int32_t currentTime = getCurrentTime();
        auto listAction = annotationListPanel_->render(annotationManager_.get(), currentTime);

        if (listAction.snapToId != 0) {
            const Annotation* ann = annotationManager_->getAnnotation(listAction.snapToId);
            if (ann && ann->positionValid) {
                camera3D_->setTarget(ann->currentX, ann->currentY, 0.0f);
                // Disable camera follow so user can inspect the annotation
                uiWindowManager_->getCameraSettings().enableFollow = false;
            }
        }

        if (listAction.editId != 0) {
            if (annotationPropertiesPanel_) {
                annotationPropertiesPanel_->setVisible(true);
            }
        }

        if (listAction.exportRequested) {
            annotationManager_->exportManualAnnotations();
        }
    }
}

// Left drag pans, right drag rotates, middle drag pans, wheel zooms.
// A left click selects the nearest actor; a right click opens the annotation menu.
void MapVisualizer::handleMouseInput() {
    ImGuiIO& io = ImGui::GetIO();
    const auto& cameraSettings = uiWindowManager_->getCameraSettings();

    if (io.MouseClicked[0]) {
        leftClickStartPos_ = io.MousePos;
        leftClickStarted_ = true;
    }

    if (io.MouseDown[0]) {
        camera3D_->moveRelative(-io.MouseDelta.x * cameraSettings.panSensitivity,
                                -io.MouseDelta.y * cameraSettings.panSensitivity);
    }

    if (io.MouseReleased[0] && leftClickStarted_) {
        if (distanceBetween(io.MousePos, leftClickStartPos_) < CLICK_VS_DRAG_THRESHOLD) {
            selectActorAt(io.MousePos);
        }
        leftClickStarted_ = false;
    }

    if (io.MouseWheel != 0.0f) {
        camera3D_->zoom(-io.MouseWheel * kWheelZoomStep);
    }

    if (io.MouseClicked[1]) {
        rightClickStartPos_ = io.MousePos;
        rightClickStarted_ = true;
    }

    if (io.MouseDown[1] && rightClickStarted_) {
        camera3D_->rotate(-io.MouseDelta.x * kMouseRotateSensitivity, -io.MouseDelta.y * kMouseRotateSensitivity);
    }

    if (io.MouseReleased[1] && rightClickStarted_) {
        if (distanceBetween(io.MousePos, rightClickStartPos_) < CLICK_VS_DRAG_THRESHOLD) {
            openAnnotationMenuAt(io.MousePos);
        }
        rightClickStarted_ = false;
    }

    if (io.MouseDown[2]) {
        camera3D_->moveRelative(-io.MouseDelta.x * kMiddleDragPanSensitivity, -io.MouseDelta.y * kMiddleDragPanSensitivity);
    }
}

void MapVisualizer::selectActorAt(const ImVec2& screenPos) {
    auto nearestActor = findNearestActorAtScreenPos(screenPos, true);
    if (!nearestActor.has_value()) {
        return;
    }

    uiWindowManager_->getActorSelector()->setSelectedActor(*nearestActor);

    // Also the target for the "Taken By" view on every meter panel
    for (UIMeterPanel* meterPanel : {uiWindowManager_->getMeterPanel1(),
                                     uiWindowManager_->getMeterPanel2(),
                                     uiWindowManager_->getMeterPanel3()}) {
        if (meterPanel) {
            meterPanel->setSelectedTarget(*nearestActor);
        }
    }
}

void MapVisualizer::openAnnotationMenuAt(const ImVec2& screenPos) {
    float worldX = 0.0f, worldY = 0.0f;
    if (mouseRaycaster_) {
        auto hit = mouseRaycaster_->raycastToFloor(screenPos.x, screenPos.y, 0.0f);
        if (hit.has_value()) {
            worldX = hit->worldX;
            worldY = hit->worldY;
        }
    }

    // Any actor near the click, hidden ones included, so the menu can offer it
    std::string nearbyActorGuid = findNearestActorAtScreenPos(screenPos, false).value_or("");
    std::string nearbyActorName;
    uint16_t nearbyActorSpecId = 0;

    if (!nearbyActorGuid.empty()) {
        const auto& guidToName = frameDataProvider_->getGuidToTruncatedName();
        auto nameIt = guidToName.find(nearbyActorGuid);
        nearbyActorName = (nameIt != guidToName.end()) ? std::string(nameIt->second) : nearbyActorGuid;

        if (colorGenerator_) {
            nearbyActorSpecId = colorGenerator_->getSpecId(nearbyActorGuid);
        }
    }

    if (annotationContextMenu_) {
        annotationContextMenu_->openAt(worldX, worldY, nearbyActorGuid, nearbyActorName, nearbyActorSpecId);
    }
}

void MapVisualizer::handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Space: Toggle play/pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        playbackController_->togglePlayPause();
    }

    // Q/E: Skip back/forward
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
        int32_t current = playbackController_->getCurrentTime();
        int32_t minTime = playbackController_->getMinTime();
        playbackController_->setCurrentTime(std::max(current - kKeyboardSkipMs, minTime));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        int32_t current = playbackController_->getCurrentTime();
        int32_t maxTime = playbackController_->getMaxTime();
        playbackController_->setCurrentTime(std::min(current + kKeyboardSkipMs, maxTime));
    }

    // 1-5: Playback speed presets (matches existing snap targets)
    if (ImGui::IsKeyPressed(ImGuiKey_1)) playbackController_->setSpeed(0.25f);
    if (ImGui::IsKeyPressed(ImGuiKey_2)) playbackController_->setSpeed(0.5f);
    if (ImGui::IsKeyPressed(ImGuiKey_3)) playbackController_->setSpeed(1.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_4)) playbackController_->setSpeed(2.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_5)) playbackController_->setSpeed(4.0f);

    // WASD: Camera movement (continuous while held). The speed is per
    // second so it doesn't change with frame rate. The delta is capped so a
    // stalled frame doesn't fling the camera.
    constexpr float keyboardPanUnitsPerSecond = 40.0f;
    const float keyboardPanSpeed = keyboardPanUnitsPerSecond * std::min(io.DeltaTime, 0.1f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) camera3D_->moveRelative(0.0f, -keyboardPanSpeed);
    if (ImGui::IsKeyDown(ImGuiKey_S)) camera3D_->moveRelative(0.0f, keyboardPanSpeed);
    if (ImGui::IsKeyDown(ImGuiKey_A)) camera3D_->moveRelative(-keyboardPanSpeed, 0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_D)) camera3D_->moveRelative(keyboardPanSpeed, 0.0f);

    // R: Toggle camera auto-follow
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        auto& camSettings = uiWindowManager_->getCameraSettings();
        camSettings.enableFollow = !camSettings.enableFollow;
    }

    // F1 or Shift+?: Toggle help panel
    if (ImGui::IsKeyPressed(ImGuiKey_F1) ||
        (ImGui::IsKeyDown(ImGuiMod_Shift) && ImGui::IsKeyPressed(ImGuiKey_Slash))) {
        if (uiWindowManager_->getHelpPanel()) {
            uiWindowManager_->getHelpPanel()->toggle();
        }
    }

    // F5: Refresh log and return to encounter selector
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        shouldRefreshEncounters_ = true;
    }
}

std::optional<std::string> MapVisualizer::findNearestActorAtScreenPos(const ImVec2& screenPos,
                                                                      bool renderedActorsOnly) const {
    if (!frameDataProvider_ || !frameDataProvider_->hasPositions() || !camera3D_) {
        return std::nullopt;
    }

    ImGuiIO& io = ImGui::GetIO();
    const auto& actorPositions = frameDataProvider_->getActorPositions();

    float closestDistSq = kScreenProximityThresholdSq;
    std::string nearestActorGuid;

    for (const auto& [guid, pos] : actorPositions) {
        if (!pos.hasPosition) continue;

        // Same visibility logic as SceneRenderer3D
        if (renderedActorsOnly) {
            ActorType actorType = ActorType::Unknown;
            if (const ActorType* typePtr = colorGenerator_->getActorType(guid)) {
                actorType = *typePtr;
            }

            UnitReaction reaction = UnitReaction::Neutral;
            if (const UnitFlags* flagsPtr = colorGenerator_->getActorFlags(guid)) {
                reaction = flagsPtr->reaction;
            }

            if (!uiWindowManager_->getVisibilitySettings().shouldRender(actorType, reaction)) {
                continue;
            }
        }

        auto projection = camera3D_->worldToScreen(pos.x, pos.y, 0.0f,
                                                    io.DisplaySize.x, io.DisplaySize.y);
        if (!projection.visible) continue;

        float dx = projection.x - screenPos.x;
        float dy = projection.y - screenPos.y;
        float distSq = dx * dx + dy * dy;

        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            nearestActorGuid = std::string(guid);
        }
    }

    if (!nearestActorGuid.empty()) {
        return nearestActorGuid;
    }
    return std::nullopt;
}
