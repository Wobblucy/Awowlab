#include "Core/UIManager.h"
#include "Core/ErrorLogger.h"
#include "../../Database/EventDatabase.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/OutputWriter.h"
#include "../../Parser/Include/Encounter/EncounterSegment.h"
#include "AnnotationManager/include/TriggerEvaluators/TriggerEvaluatorCommon.h"
#include "UI/Panels/Navigation/UIEncounterSelector.h"
#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/Panels/WarcraftLogs/UIWarcraftLogsPanel.h"
#include "UI/SpellIconLoader.h"
#include "UI/ClassIconCache.h"
#include "UI/UISettings.h"
#include "Core/Utils/ExecutablePath.h"
#include "Core/WarcraftLogsSession.h"
#include "Core/UnifiedSettings.h"
#include "PlayAlong/PlayAlongVisualizer.h"
#include "PlayAlong/UIKeybindConfigModal.h"
#include "Core/CurrentEncounter.h"
#include "Core/Benchmark/BenchmarkSession.h"
#include "Core/StringInterner.h"
#include "Core/LocalizationManager.h"
#include "Parser/SpecializationColors.h"
#include "UI/AwlUI/Theme.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/Benchmark/UIClassificationReview.h"
#include "UI/Benchmark/UIMentorPreview.h"
#include "UI/Benchmark/UIPrepareWizard.h"
#include "UI/Benchmark/UIBenchmarkReportCard.h"
#include "UI/Benchmark/ReportContextBuilder.h"
#include "PlayAlong/UIPlayerSelector.h"
#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <GLFW/glfw3.h>

UIManager::UIManager(int width, int height, const std::string& title)
    : windowManager(std::make_unique<WindowManager>(width, height, title))
    , vulkanRenderer(std::make_unique<VulkanRenderer>())
    , selectedEncounterIndex_(0)
{
}

UIManager::~UIManager() {
}

bool UIManager::initialize() {
    if (!windowManager->initialize()) {
        AWOW_LOG_ERROR("UI", "Failed to initialize window manager - check GLFW and display configuration");
        return false;
    }

    if (!vulkanRenderer->initialize(windowManager->getWindow())) {
        AWOW_LOG_ERROR("UI", "Failed to initialize Vulkan renderer - check GPU drivers and Vulkan support");
        return false;
    }

    AWOW_LOG_INFO("UI", "UI Manager initialized successfully");
    return true;
}

void UIManager::loadEventDatabase(const EventDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadEventDatabase(db);
    }
}

void UIManager::loadSpellCastDatabase(const SpellCastDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadSpellCastDatabase(db);
    }
}

bool UIManager::loadSpellGCDData(const std::string& csvPath) {
    if (vulkanRenderer) {
        return vulkanRenderer->loadSpellGCDData(csvPath);
    }
    return false;
}

void UIManager::loadResourceDatabase(const ResourceDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadResourceDatabase(db);
    }
}

void UIManager::loadCombatDatabase(const CombatDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadCombatDatabase(db);
    }
}

void UIManager::loadDeathDatabase(const DeathDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadDeathDatabase(db);
    }
}

void UIManager::loadAuraDatabase(const AuraDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadAuraDatabase(db);
    }
}

void UIManager::loadDispelDatabase(const DispelDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadDispelDatabase(db);
    }
}

void UIManager::loadAvoidanceDatabase(const AvoidanceDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadAvoidanceDatabase(db);
    }
}

void UIManager::loadEmoteDatabase(const EmoteDatabase* db) {
    if (vulkanRenderer) {
        vulkanRenderer->loadEmoteDatabase(db);
    }
}

void UIManager::loadCombatantInfo(const std::vector<CombatantInfoEvent>& combatantInfo) {
    if (vulkanRenderer) {
        vulkanRenderer->loadCombatantInfo(combatantInfo);
    }
}

void UIManager::generateFallbackColors() {
    if (vulkanRenderer) {
        vulkanRenderer->generateFallbackColors();
    }
}

void UIManager::loadMapChangeEvents(const std::vector<MapChangeEvent>& mapChanges, uint32_t encounterId) {
    if (vulkanRenderer) {
        vulkanRenderer->loadMapChangeEvents(mapChanges, encounterId);
    }
}

void UIManager::loadSpellNameMap(const std::unordered_map<uint32_t, std::string>& spellNameMap) {
    if (vulkanRenderer) {
        vulkanRenderer->loadSpellNameMap(spellNameMap);
    }
}

void UIManager::loadSpellDataForIds(const std::unordered_set<uint32_t>& spellIds) {
    if (vulkanRenderer) {
        auto* iconLoader = vulkanRenderer->getSpellIconLoader();
        if (iconLoader) {
            iconLoader->loadMappingsForIds(spellIds);
        }
    }
}

void UIManager::loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    if (vulkanRenderer) {
        vulkanRenderer->loadActorNpcIdMap(actorIdToNpcId);
    }
}

void UIManager::loadGuidToNameMap(const std::unordered_map<std::string, std::string>& guidToNameMap) {
    if (vulkanRenderer) {
        vulkanRenderer->loadGuidToNameMap(guidToNameMap);
    }
}

void UIManager::loadHealthScalingData(uint64_t avgPlayerHealth,
                                       const std::unordered_map<std::string, uint64_t>& actorMaxHealth) {
    if (vulkanRenderer) {
        vulkanRenderer->loadHealthScalingData(avgPlayerHealth, actorMaxHealth);
    }
}

void UIManager::setExtractedData(const ExtractedData* data) {
    if (vulkanRenderer) {
        vulkanRenderer->setExtractedData(data);
    }
}

void UIManager::setExportCallback(ExportSnapshotCallback callback) {
    if (vulkanRenderer) {
        vulkanRenderer->setExportCallback(std::move(callback));
    }
}

bool UIManager::showEncounterSelector(const std::vector<EncounterSegment>& encounters) {
    return showEncounterSelectorWithRefresh(encounters, 0, 0, 0, nullptr);
}

bool UIManager::showEncounterSelectorWithRefresh(
    const std::vector<EncounterSegment>& encounters,
    size_t totalLines,
    size_t parsedBytes,
    size_t totalBytes,
    std::function<bool()> refreshCallback
) {
    UIEncounterSelector selector;
    selector.setEncounters(encounters);
    selector.setFileInfo(totalLines, parsedBytes, totalBytes);

    AWOW_LOG_INFO("UI", "Showing encounter selector UI...");

    std::vector<EncounterSegment> currentEncounters = encounters;

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        if (pendingFileChecker_ && pendingFileChecker_()) {
            AWOW_LOG_INFO("UI", "Pending file received while in encounter selector");
            pendingFileFlag_ = true;
            return false;  // Exit to handle pending file
        }

        if (selector.isReturnToHomeRequested()) {
            AWOW_LOG_INFO("UI", "Return to home requested");
            return false;  // Causes caller to return to landing page
        }

        if (selector.isRefreshRequested()) {
            selector.clearRefreshRequest();

            if (refreshCallback) {
                AWOW_LOG_INFO("UI", "Refresh requested, calling callback...");
                bool foundNew = refreshCallback();

                if (foundNew) {
                    // The caller re-fetches and shows the selector again
                    return false;
                } else {
                    AWOW_LOG_INFO("UI", "No new encounters found.");
                }
            }
        }

        bool selectionMade = vulkanRenderer->drawFrameWithEncounterSelector(&selector);

        if (selectionMade) {
            selectedEncounterIndex_ = selector.getSelectedEncounterIndex();
            AWOW_LOG_INFO("UI", "Encounter selected");
            return true;
        }
    }

    return false;
}

size_t UIManager::getSelectedEncounterIndex() const {
    return selectedEncounterIndex_;
}

void UIManager::setWindowTitle(const std::string& title) {
    if (windowManager) {
        windowManager->setWindowTitle(title);
    }
}

void* UIManager::getGlfwWindow() const {
    if (windowManager) {
        return windowManager->getWindow();
    }
    return nullptr;
}

void UIManager::run() {
    AWOW_LOG_INFO("UI", "Starting render loop...");

    // Frame rate limiting (30 FPS target)
    constexpr double TARGET_FRAME_TIME = 1.0 / 30.0;
    double lastFrameTime = glfwGetTime();

    while (!windowManager->shouldClose()) {
        double currentTime = glfwGetTime();
        double elapsed = currentTime - lastFrameTime;
        if (elapsed < TARGET_FRAME_TIME) {
            double sleepTime = TARGET_FRAME_TIME - elapsed;
            if (sleepTime > 0.001) {
                glfwWaitEventsTimeout(sleepTime);
            }
            continue;
        }
        lastFrameTime = currentTime;

        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        vulkanRenderer->drawFrame();

        // Check for auto-save of settings (every 10s if dirty)
        SettingsCache::instance().tick();

        if (pendingFileChecker_ && pendingFileChecker_()) {
            AWOW_LOG_INFO("UI", "Pending file received from another instance, exiting render loop");
            pendingFileFlag_ = true;
            break;
        }

        if (vulkanRenderer->shouldReturnToEncounterSelector() ||
            vulkanRenderer->shouldReturnToLandingPage() ||
            vulkanRenderer->shouldRefreshEncounters()) {
            AWOW_LOG_INFO("UI", "User requested navigation, exiting render loop");
            break;
        }
    }

    // Wait for GPU to finish all in-flight frames before exiting
    // This prevents semaphore reuse issues when transitioning between replays
    vulkanRenderer->waitForDeviceIdle();

    AWOW_LOG_INFO("UI", "Render loop ended");
}

bool UIManager::shouldReturnToEncounterSelector() const {
    if (vulkanRenderer) {
        return vulkanRenderer->shouldReturnToEncounterSelector();
    }
    return false;
}

bool UIManager::shouldReturnToLandingPage() const {
    if (vulkanRenderer) {
        return vulkanRenderer->shouldReturnToLandingPage();
    }
    return false;
}

void UIManager::resetNavigationFlags() {
    if (vulkanRenderer) {
        vulkanRenderer->resetNavigationFlags();
    }
    pendingFileFlag_ = false;
}

bool UIManager::shouldRefreshEncounters() const {
    if (vulkanRenderer) {
        return vulkanRenderer->shouldRefreshEncounters();
    }
    return false;
}

void UIManager::resetRefreshFlag() {
    if (vulkanRenderer) {
        vulkanRenderer->resetRefreshFlag();
    }
}

bool UIManager::shouldLoadPendingFile() const {
    return pendingFileFlag_;
}

void UIManager::setPendingFileChecker(std::function<bool()> checker) {
    pendingFileChecker_ = std::move(checker);
}

void UIManager::unloadDatabases() {
    if (vulkanRenderer) {
        vulkanRenderer->unloadDatabases();
    }
}

AutoAnnotationTriggerManager* UIManager::getAutoAnnotationTriggerManager() {
    return vulkanRenderer ? vulkanRenderer->getAutoAnnotationTriggerManager() : nullptr;
}

LandingPageResult UIManager::showLandingPage(warcraftlogs::ResponseCache* responseCache,
                                               bool autoOpenWclDialog) {
    UILandingPage landingPage;

    ClassIconCache classIconCache;
    std::string iconPath = awow::getExecutableDirectory() + "PNG\\Spell Icons";
    if (classIconCache.initialize(
            vulkanRenderer->getDevice(),
            vulkanRenderer->getPhysicalDevice(),
            vulkanRenderer->getGraphicsQueue(),
            vulkanRenderer->getCommandPool(),
            iconPath)) {
        landingPage.setClassIconCache(&classIconCache);
    }

    // Carry over whatever the last action reported, then clear it so the
    // message shows once rather than on every return to the menu.
    landingPage.setPackageStatus(landingStatus_, landingStatusIsError_);
    landingStatus_.clear();
    landingStatusIsError_ = false;

    if (responseCache) {
        landingPage.setResponseCache(responseCache);
    }

    if (autoOpenWclDialog) {
        landingPage.setAutoOpenWclDialog(true);
    }

    AWOW_LOG_INFO("UI", "Showing landing page...");

    LandingPageResult result{LandingPageChoice::Exit, ""};

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        if (pendingFileChecker_ && pendingFileChecker_()) {
            AWOW_LOG_INFO("UI", "Pending file received while on landing page");
            result = LandingPageResult{LandingPageChoice::LoadPendingFile, ""};
            break;
        }

        bool selectionMade = vulkanRenderer->drawFrameWithLandingPage(&landingPage);

        if (selectionMade) {
            result = landingPage.getResult();
            AWOW_LOG_INFO("UI", "Landing page choice made: " + std::to_string(static_cast<int>(result.choice)));
            break;
        }
    }

    classIconCache.cleanup();

    return result;
}

int32_t UIManager::showFightSelector(warcraftlogs::WarcraftLogsSession* session) {
    UIWarcraftLogsPanel fightSelector;
    fightSelector.setSession(session);

    AWOW_LOG_INFO("UI", "Showing WarcraftLogs fight selector...");

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        if (pendingFileChecker_ && pendingFileChecker_()) {
            AWOW_LOG_INFO("UI", "Pending file received while in fight selector");
            pendingFileFlag_ = true;
            return -2;  // Special return value to indicate pending file
        }

        bool selectionMade = vulkanRenderer->drawFrameWithFightSelector(&fightSelector);

        if (selectionMade) {
            int32_t fightId = fightSelector.getSelectedFightId();
            AWOW_LOG_INFO("UI", "Fight selected: " + std::to_string(fightId));
            return fightId;
        }

        if (fightSelector.isCancelled()) {
            AWOW_LOG_INFO("UI", "Fight selection cancelled");
            return -1;
        }
    }

    return -1;
}

bool UIManager::showLoadingOverlay(
    const std::string& title,
    std::function<bool(std::function<void(const std::string&, float)>)> task
) {
    std::string currentMessage = "Initializing...";
    float currentProgress = -1.0f;  // Indeterminate
    bool taskSuccess = false;

    auto progressCallback = [&](const std::string& msg, float progress) {
        currentMessage = msg;
        currentProgress = progress;

        if (!windowManager->shouldClose()) {
            windowManager->pollEvents();

            if (windowManager->wasResized()) {
                vulkanRenderer->handleResize();
                windowManager->resetResizedFlag();
            }

            vulkanRenderer->drawFrameWithProgress(title, currentMessage, currentProgress);
        }
    };

    taskSuccess = task(progressCallback);

    return taskSuccess && !windowManager->shouldClose();
}

void UIManager::renderProgressFrame(const std::string& message, float progress) {
    if (windowManager->shouldClose()) {
        return;
    }

    windowManager->pollEvents();

    if (windowManager->wasResized()) {
        vulkanRenderer->handleResize();
        windowManager->resetResizedFlag();
    }

    vulkanRenderer->drawFrameWithProgress("Loading", message, progress);
}

baseline_ui::SpecSelection UIManager::pickBaselineSpec() {
    // Same modal-loop shape as the player picker: the landing page is not
    // running at this point, so this drives its own frames.
    baseline_ui::UISpecSelector selector;
    selector.open();

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        bool finished = false;
        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            finished = selector.render();
        });

        if (finished) return selector.getSelection();
    }

    return {};   // Window closed
}

PlayAlong::PlayerSelection UIManager::pickBenchmarkPlayer(
    const std::vector<PlayAlong::PlayerInfo>& players) {

    // Same selector Play Along uses. Picking a player out of a report is the
    // same job in both cases, and a second list would drift away from this one.
    PlayAlong::UIPlayerSelector selector;
    selector.setPlayers(players);
    selector.open();

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        bool finished = false;
        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            finished = selector.render();
        });

        if (finished) return selector.getSelection();
    }

    return {};   // Window closed
}

void UIManager::runClassificationReview(
    uint16_t specId,
    const std::unordered_map<uint32_t, std::string>& spellNames) {

    benchmark_ui::UIClassificationReview review;
    benchmark_ui::ClassificationReviewContext context;
    context.specId = specId;
    // The referenced map is the session's, which outlives this modal loop.
    context.spellNames = &spellNames;

    review.setVisible(true);

    while (!windowManager->shouldClose() && review.isVisible()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            // Returns true the frame everything is confirmed. Closing early is
            // allowed - the reference is already saved, and the comparison
            // blocks until the rest are ruled on.
            if (review.render(context)) {
                review.setVisible(false);
            }
        });
    }
}

bool UIManager::runPrepareWizard(
    uint16_t specId,
    const std::string& playerName,
    const std::unordered_map<uint32_t, std::string>& spellNames) {

    benchmark_ui::UIPrepareWizard wizard;
    benchmark_ui::PrepareWizardContext context;
    context.specId = specId;
    context.playerName = playerName;
    context.spellNames = spellNames;
    context.encounterName = CurrentEncounter::instance().getName();
    context.encounterId = CurrentEncounter::instance().getId();
    context.phaseData.encounterId = context.encounterId;
    context.phaseData.spellNames = &context.spellNames;

    // What this pull offers as phase triggers.
    //
    // The editor only shows a section when its list has something in it, so
    // leaving these empty left the step offering fixed-time splits and
    // nothing else - which is not much use for a boundary that has to
    // resolve the same way in two different logs.
    if (const ExtractedData* data = benchmark::BenchmarkSession::instance()
                                        .liveExtractedData()) {
        context.events = data;

        int32_t pullEnd = 0;
        for (const auto& cast : data->spellCastEvents) {
            pullEnd = std::max(pullEnd, cast.end_timestamp_ms);
        }
        context.phaseData.ruleInputs.pullStart_ms = 0;
        context.phaseData.ruleInputs.pullEnd_ms = pullEnd;

        for (const auto& emote : data->emoteEvents) {
            if (emote.text.empty()) continue;
            context.phaseData.emotes.push_back(
                {emote.timestamp_ms, emote.source_name, emote.text});
        }

        // Who counts as the enemy, taken from the unit flags the log already
        // carries rather than guessed from the GUID prefix. Keyed by npc id
        // because an import's cast events carry a different GUID for a mob
        // than its position events do.
        const std::unordered_set<uint32_t> hostileNpcIds = data->hostileNpcIds();

        // First cast of each hostile ability, which is what a phase rule can
        // actually key on. Later casts of the same spell are irrelevant: the
        // rule fires on the first one.
        std::unordered_set<uint32_t> seen;
        for (const auto& cast : data->spellCastEvents) {
            const uint32_t casterNpcId = data->npcIdOf(cast.actor_guid);
            if (casterNpcId == 0) continue;
            if (!hostileNpcIds.count(casterNpcId)) continue;
            if (cast.end_timestamp_ms < 0) continue;
            if (!seen.insert(cast.spell_id).second) continue;

            std::string name;
            if (auto it = context.spellNames.find(cast.spell_id);
                it != context.spellNames.end()) {
                name = it->second;
            }
            context.phaseData.casts.push_back(
                {cast.spell_id, cast.end_timestamp_ms, std::move(name)});
        }

        std::sort(context.phaseData.casts.begin(), context.phaseData.casts.end(),
                  [](const auto& a, const auto& b) {
                      return a.time_ms < b.time_ms;
                  });

        // Resolve saved rules against this pull so each one shows the time it
        // lands on. Both lists already hold one row per trigger, so a linear
        // scan finds the first occurrence directly.
        //
        // These capture `context` by reference, and are read during the render
        // loop below while it is still in scope.
        context.phaseData.ruleInputs.firstCastTime =
            [&context](uint32_t spellId) -> std::optional<int32_t> {
                for (const auto& row : context.phaseData.casts) {
                    if (row.spellId == spellId) return row.time_ms;
                }
                return std::nullopt;
            };
        context.phaseData.ruleInputs.firstEmoteTime =
            [&context](std::string_view text) -> std::optional<int32_t> {
                for (const auto& row : context.phaseData.emotes) {
                    if (row.text == text) return row.time_ms;
                }
                return std::nullopt;
            };
    }

    wizard.open();
    bool confirmed = false;
    bool reviewRequested = false;

    while (!windowManager->shouldClose() && wizard.isOpen()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        // Deferred out of the frame, as in runLiveCompareReport
        if (reviewRequested) {
            reviewRequested = false;
            runClassificationReview(specId, spellNames);
        }

        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            const auto result = wizard.render(context);
            if (result == benchmark_ui::PrepareResult::Confirmed) confirmed = true;
        });

        reviewRequested = wizard.consumeReviewRequest();
    }

    return confirmed;
}

bool UIManager::runMentorPreview(
    const std::unordered_map<uint32_t, std::string>& spellNames) {

    auto& session = benchmark::BenchmarkSession::instance();
    if (!session.mentorProfile()) return false;

    benchmark_ui::UIMentorPreview preview;
    benchmark_ui::MentorPreviewContext context;
    context.spellNames = spellNames;
    context.encounterName = CurrentEncounter::instance().getName();
    context.iconLoader = vulkanRenderer ? vulkanRenderer->getSpellIconLoader()
                                        : nullptr;

    preview.setVisible(true);
    bool reviewRequested = false;

    while (!windowManager->shouldClose() && preview.isVisible()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        // Re-read each frame: a re-cut after the review reassigns the profile,
        // and a pointer captured once would be left dangling.
        context.profile = session.mentorProfile();
        if (!context.profile) break;

        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            const auto action = preview.render(context);
            if (action == benchmark_ui::PreviewAction::ReopenPrepare) {
                // Deferred out of the frame, as in runLiveCompareReport
                reviewRequested = true;
                preview.setVisible(false);
            }
        });
    }

    return reviewRequested;
}

void UIManager::runBenchmarkReport() {
    runLiveCompareReport(nullptr);
}

void UIManager::runLiveCompareReport(const std::function<std::string()>& onPoll) {
    auto& session = benchmark::BenchmarkSession::instance();

    benchmark_ui::UIBenchmarkReportCard card;
    benchmark_ui::ReportContext context;

    // Rebuilt every frame rather than captured once. The context holds raw
    // pointers into the session, and recompare() reassigns the profiles and
    // the result behind them - a cached pointer would be left dangling the
    // moment the user cleared a blocker from inside this loop.
    SpellIconLoader* iconLoader =
        vulkanRenderer ? vulkanRenderer->getSpellIconLoader() : nullptr;
    const auto refreshContext = [&]() {
        context = benchmark_ui::buildReportContext(session, iconLoader);
    };
    refreshContext();

    card.setVisible(true);

    // Set inside the frame callback, acted on after it closes. The review runs
    // its own render loop, and starting one from within an open frame nests
    // two render loops, which crashes.
    bool reviewRequested = false;

    while (!windowManager->shouldClose() && card.isVisible()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        if (reviewRequested) {
            reviewRequested = false;
            runClassificationReview(session.specId(), session.spellNames());
            session.recompare();
        }

        // Live mode: swap in a newer pull if one finished. Done before the
        // context is rebuilt so the card draws the new numbers this frame
        // rather than the next one, and outside the frame callback below
        // because this can open a picker with a render loop of its own.
        std::string liveStatus;
        if (onPoll) {
            liveStatus = onPoll();
        }

        refreshContext();
        context.liveStatus = std::move(liveStatus);

        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            const auto action = card.render(context);

            switch (action) {
                case benchmark_ui::ReportAction::ReviewClasses:
                    // Blocked on classification; deferred to after this frame
                    reviewRequested = true;
                    break;

                case benchmark_ui::ReportAction::RebuildMentor:
                    // Closes the card: the rebuild happens a layer up, after
                    // this loop returns and the source log can be re-opened.
                    session.requestMentorRebuild();
                    card.setVisible(false);
                    break;

                case benchmark_ui::ReportAction::Dismiss:
                    card.setVisible(false);
                    break;

                default:
                    // OpenReplay and JumpToTime need the replay loaded, which
                    // this flow deliberately never did. Left for when the card
                    // can hand the loaded log back to the encounter view.
                    break;
            }
        });
    }
}

void UIManager::runPlayAlong(const EventDatabase* eventDb,
                              const SpellCastDatabase* spellCastDb,
                              const CombatDatabase* combatDb,
                              const AuraDatabase* auraDb,
                              const std::vector<CombatantInfoEvent>& combatantInfo,
                              const std::vector<MapChangeEvent>& mapChanges,
                              const std::vector<DeathEvent>& deathEvents,
                              const std::vector<ResurrectEvent>& resurrectEvents,
                              const std::unordered_map<uint32_t, std::string>* spellNameMap,
                              const std::string& fightName) {
    using namespace PlayAlong;

    PlayAlongVisualizer visualizer;

    bool initialized = visualizer.initialize(
        vulkanRenderer->getDevice(),
        vulkanRenderer->getPhysicalDevice(),
        vulkanRenderer->getRenderPass(),
        nullptr,  // descriptor pool not needed
        vulkanRenderer->getGraphicsQueue(),
        vulkanRenderer->getCommandPool()
    );

    if (!initialized) {
        AWOW_LOG_ERROR("UI", "Failed to initialize PlayAlong 3D rendering - Vulkan pipeline creation failed");
        return;
    }

    visualizer.setSpellIconLoader(vulkanRenderer->getSpellIconLoader());

    visualizer.loadEventDatabase(eventDb);
    visualizer.loadSpellCastDatabase(spellCastDb);

    visualizer.setEncounterInfo(CurrentEncounter::instance().getId(), fightName);

    visualizer.loadAnnotationDatabases(combatDb, auraDb, spellNameMap);

    AWOW_LOG_INFO("PLAYALONG", "Loading " + std::to_string(mapChanges.size()) + " map change events");
    visualizer.loadMapChangeEvents(mapChanges);

    visualizer.loadDeathEvents(deathEvents, resurrectEvents, eventDb);

    visualizer.setCombatantInfo(combatantInfo, eventDb);

    visualizer.getPlayerSelector().open();

    windowManager->setWindowTitle("AwowLab - Play Along: " + fightName);

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        bool selectionComplete = false;

        vulkanRenderer->drawFrameWithImGuiCallback([&]() {
            selectionComplete = visualizer.getPlayerSelector().render();
        });

        if (selectionComplete) {
            if (visualizer.getPlayerSelector().wasCancelled()) {
                AWOW_LOG_INFO("PLAYALONG", "Player selection cancelled");
                return;
            }

            const auto& selection = visualizer.getPlayerSelector().getSelection();
            if (selection.selected) {
                AWOW_LOG_INFO("PLAYALONG", "Selected player: " + selection.playerName + " (GUID: " + selection.playerGuid + ")");

                visualizer.setTargetPlayer(selection.playerGuid, selection.playerName);

                UIKeybindConfigModal keybindModal;
                keybindModal.open(selection.specId, selection.playerGuid, spellCastDb);

                while (!windowManager->shouldClose() && keybindModal.isOpen()) {
                    windowManager->pollEvents();

                    if (windowManager->wasResized()) {
                        vulkanRenderer->handleResize();
                        windowManager->resetResizedFlag();
                    }

                    vulkanRenderer->drawFrameWithImGuiCallback([&]() {
                        keybindModal.render(vulkanRenderer->getSpellIconLoader());
                    });
                }

                if (keybindModal.wasCancelled()) {
                    AWOW_LOG_INFO("PLAYALONG", "Keybind configuration cancelled");
                    return;
                }

                visualizer.setKeybindSettings(keybindModal.getKeybindSettings());

                break;
            }
        }
    }

    visualizer.resetNavigationFlags();

    while (!windowManager->shouldClose() && !visualizer.shouldReturnToLandingPage()) {
        windowManager->pollEvents();

        if (windowManager->wasResized()) {
            vulkanRenderer->handleResize();
            windowManager->resetResizedFlag();
        }

        if (pendingFileChecker_ && pendingFileChecker_()) {
            AWOW_LOG_INFO("PLAYALONG", "Pending file received while in Play Along mode");
            pendingFileFlag_ = true;
            break;
        }

        vulkanRenderer->drawFrameWith3DAndImGui(
            [&](VkCommandBuffer cmd, VkExtent2D extent) {
                visualizer.render3D(cmd, extent.width, extent.height);
            },
            [&]() {
                visualizer.render();
            }
        );
    }
}
