#include "Application.h"
#include "FileDialog.h"
#include "AwowLabBuildOptions.h"
#include "Database/AnalysisPackage/PackageBuilder.h"
#include "Models/SeasonZones.h"
#include "Models/Fight.h"
#include "Version.h"
#include "Models/RankingSample.h"
#include "LogSession.h"
#include "WarcraftLogsSession.h"
#include "PerfMonitor.h"
#include "LocalizationManager.h"
#include "SpellNameDatabase.h"
#include "HeroTalents.h"
#include "ErrorLogger.h"
#include "CurrentEncounter.h"
#include "EncounterDataLoader.h"
#include "FileAssociation.h"
#include "SingleInstance.h"
#include "Overlay/OverlayApplication.h"
#include "Overlay/LiveLogManager.h"
#include "Overlay/PullSegment.h"
#include "Benchmark/BenchmarkSession.h"
#include "Parser/Include/Parser/SpecializationColors.h"
#include "Utils/FileNameUtils.h"

#include <future>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include "Settings/WclSettings.h"
#include "Database/OutputWriter.h"
#include "Database/HasteBuffData.h"
#include "SnapshotManager/include/SnapshotImporter.h"
#include "SnapshotManager/include/SnapshotExporter.h"
#include "AnnotationManager/include/AnnotationTypes.h"  // For Annotation struct
#include "AnnotationManager/include/AutoAnnotationTypes.h"  // For AutoAnnotationTrigger struct
#include "AnnotationManager/include/AutoAnnotationTriggerManager.h"  // For direct access to trigger manager
#include "Database/CooldownModifiers/CooldownModifierRegistry.h"  // For early WCL loading
#include "Parser/Include/Encounter/EncounterScanner.h"
#include "Parser/Include/Encounter/EncounterSegment.h"
#include "UIManager/include/Core/BrowserHelper.h"
#include "UIManager/include/Core/UIManager.h"
#include "UIManager/include/UI/SpellLinkOpener.h"
#include "UIManager/include/UI/UISettings.h"
#include "UIManager/include/UI/Panels/Navigation/UILandingPage.h"
#include "UIManager/include/UI/Panels/WarcraftLogs/UIWarcraftLogsUrlDialog.h"
#include "warcraftlogs_manager/include/Auth/OAuthClient.h"
#include "warcraftlogs_manager/include/Storage/ResponseCache.h"
#include "Utils/ExecutablePath.h"
#include <GLFW/glfw3.h>
#include <filesystem>

namespace {
#if AWOWLAB_ENABLE_UPDATER
// Install pending updater if one was staged by the auto-updater
void installPendingUpdater() {
    std::string exeDir = awow::getExecutableDirectory();
    std::filesystem::path pendingPath = std::filesystem::path(exeDir) / "updater" / "pending" / "AwowLabUpdater.exe";
    std::filesystem::path targetPath = std::filesystem::path(exeDir) / "updater" / "AwowLabUpdater.exe";

    if (std::filesystem::exists(pendingPath)) {
        std::error_code ec;
        std::filesystem::copy_file(pendingPath, targetPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            std::filesystem::remove(pendingPath, ec);
            AWOW_LOG_INFO("UPDATER", "Installed pending updater update");
        } else {
            AWOW_LOG_WARNING("UPDATER", "Failed to install pending updater: " + ec.message());
        }
    }
}
#endif

// Report data cached on disk, relative to the working directory
constexpr const char* kWclCacheDir = "./wcl_cache";

// Used when nothing is saved and the monitor cannot be asked
constexpr int kFallbackWindowWidth = 1280;
constexpr int kFallbackWindowHeight = 720;
// A first-run window takes this share of the primary monitor
constexpr double kDefaultWindowScreenFraction = 0.8;

std::string reportFightUrl(const std::string& reportCode, int32_t fightId) {
    return "https://www.warcraftlogs.com/reports/" + reportCode
         + "#fight=" + std::to_string(fightId);
}

// No expiry check: a stale token surfaces as a failed request, which is
// where the sign-in prompt belongs.
std::string storedWclAccessToken() {
    return WclSettings::readFrom(SettingsCache::instance().get()).accessToken;
}

// A failed query that looks like a rejected sign-in marks the stored token
// invalid, so the dialog asks the user to sign in again.
// Report queries carry @onlySpendUserPoints, so WarcraftLogs refuses them
// once the signed-in user's points for the hour are spent instead of
// charging the app's own limit.
bool isUserPointsError(const std::string& error) {
    std::string lower = error;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower.find("status 429") != std::string::npos ||
           lower.find("user points") != std::string::npos ||
           lower.find("onlyspenduserpoints") != std::string::npos;
}

// Tells the user their points ran out and when they come back, through the
// WCL dialog. The sign-in stays valid.
void noteUserPointsSpent(warcraftlogs::WarcraftLogsSession& session) {
    const int seconds = session.userPointsResetIn();
    awow::wclNotice() = seconds > 0
        ? LF("wcl.user_points_spent", (seconds + 59) / 60)
        : std::string(L("wcl.user_points_spent_unknown"));
}

void persistIfAuthError(const std::string& error) {
    if (error.find("401") != std::string::npos ||
        error.find("Unauthorized") != std::string::npos ||
        error.find("auth") != std::string::npos ||
        error.find("token") != std::string::npos) {
        UIWarcraftLogsUrlDialog::persistAuthError("Query failed: " + error);
    }
}

// Global encounter info for a local log's encounter, read by trigger
// persistence and snapshot export
void publishCurrentEncounter(const EncounterSegment& encounter) {
    auto& current = CurrentEncounter::instance();
    current.set(
        encounter.encounterName,
        encounter.encounterId,
        static_cast<uint16_t>(encounter.difficultyId),
        0,  // mapId will be derived from mapChangeEvents during export
        encounter.endTimestamp_ms - encounter.startTimestamp_ms
    );

    current.setKill(encounter.isSuccess);
    if (encounter.isMythicPlus()) {
        current.setMythicPlusInfo(encounter.keystoneLevel);
    }
}

// Spell names for an analysis package come from the shipped database rather
// than the log, so packages from every source read the same.
std::function<std::string_view(uint32_t)> shippedSpellNameResolver() {
    return [](uint32_t id) -> std::string_view {
        auto& spellDb = SpellNameDatabase::instance();
        return spellDb.hasSpellName(id) ? std::string_view(spellDb.getSpellName(id))
                                        : std::string_view{};
    };
}

// Snapshot export, the same from every view that can save one
void exportSnapshot(const ExtractedData* data,
                    const std::vector<Annotation>& annotations,
                    const std::vector<AutoAnnotationTrigger>& triggers,
                    const std::vector<CachedAnnotationEvent>& cachedEvents,
                    int32_t startTimeMs,
                    int32_t endTimeMs) {
    if (!data) {
        AWOW_LOG_ERROR("EXPORT", "No data available for export");
        return;
    }

    const auto& encounter = CurrentEncounter::instance();
    std::string defaultFilename = encounter.generateSnapshotFilename(startTimeMs, endTimeMs);

    std::string outputPath = FileDialog::saveSnapshotFile(defaultFilename);
    if (outputPath.empty()) {
        AWOW_LOG_INFO("EXPORT", "Export cancelled by user.");
        return;
    }

    snapshot::ExportOptions options;
    options.windowStartMs = startTimeMs;
    options.windowEndMs = endTimeMs;
    options.encounterId = encounter.getId();
    options.encounterName = encounter.getName();
    options.difficulty = encounter.getDifficulty();
    options.encounterDurationMs = encounter.getDurationMs();
    options.mapId = encounter.getMapId();  // Use the currently loaded map

    snapshot::SnapshotExporter exporter;
    auto result = exporter.exportSnapshot(*data, annotations, triggers, cachedEvents, options, outputPath);

    if (result.success) {
        AWOW_LOG_INFO("EXPORT", "Snapshot exported successfully to: " + outputPath);
        AWOW_LOG_INFO("EXPORT", "  Uncompressed: " + std::to_string(result.uncompressedSize / 1024) + " KB");
        AWOW_LOG_INFO("EXPORT", "  Compressed: " + std::to_string(result.compressedSize / 1024) + " KB");
    } else {
        AWOW_LOG_ERROR("EXPORT", "Snapshot export failed: " + result.errorMessage);
    }
}

} // anonymous namespace

Application::Application(int width, int height, const std::string& title)
    : width_(width)
    , height_(height)
    , title_(title)
    , uiManager_(nullptr)
    , logSession_(nullptr)
    , singleInstance_(std::make_unique<awow::SingleInstance>())
{
    singleInstance_->tryAcquire();
    // Wowhead shortcuts open in the browser; offline builds skip this
    // registration and the shortcuts stay hidden
    ui::setSpellLinkOpener([](uint32_t spellId) {
        BrowserHelper::openWowheadSpell(spellId);
    });
    // Initialize the settings cache (loads from disk, migrates legacy if needed)
    SettingsCache::instance().initialize();
    // Move any plain-text WCL token from older builds into DPAPI-protected storage
    awow::moveWclTokenToProtectedStorage();

    // Initialize error logger (log file goes next to executable)
    ErrorLogger::instance().initialize(awow::getExecutableDirectory());

    // Initialize localization (must happen before UI is created)
    auto& locMgr = LocalizationManager::instance();
    if (!locMgr.loadTranslations("data/lang")) {
        AWOW_LOG_WARNING("LOCALE", "Localization files not found in data/lang - using English keys as fallback");
    }

    const auto& settings = SettingsCache::instance().get();
    if (!settings.locale.empty()) {
        auto savedLocale = LocalizationManager::parseLocale(settings.locale);
        if (savedLocale) {
            locMgr.setLocale(*savedLocale);
        }
    }

    // Set spell name database directory and locale (lazy loading - names loaded on demand after parsing)
    auto& spellDb = SpellNameDatabase::instance();
    spellDb.setDataDirectory("data/spellnames");
    spellDb.setSpellNameLocale(locMgr.getLocale());  // Match UI locale

    // Percentage haste buffs for the spell data version in use. Every log path
    // reads them through the haste tracker, so they are loaded before any is.
    awow::loadHasteBuffs(awow::getExecutableDirectory() + "data\\haste_buffs_" +
                         settings.spellDataVersion + ".json");

    loadSettings();
}

void Application::loadSettings() {
    auto& cache = SettingsCache::instance();
    const auto& settings = cache.get();

    if (settings.windowWidth > 0 && settings.windowHeight > 0) {
        width_ = settings.windowWidth;
        height_ = settings.windowHeight;
        AWOW_LOG_INFO("APP", "Loaded window size from settings: " + std::to_string(width_) + "x" + std::to_string(height_));
    } else {
        // No saved settings - size to a share of the primary monitor
        AWOW_LOG_INFO("APP", "No valid window size in settings, calculating default.");

        // Need to init GLFW temporarily to get monitor info
        if (!glfwInit()) {
            AWOW_LOG_ERROR("GLFW", "Failed to initialize GLFW for screen resolution detection");
            width_ = kFallbackWindowWidth;
            height_ = kFallbackWindowHeight;
        } else {
            GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
            if (primaryMonitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
                if (mode) {
                    width_ = static_cast<int>(mode->width * kDefaultWindowScreenFraction);
                    height_ = static_cast<int>(mode->height * kDefaultWindowScreenFraction);
                    AWOW_LOG_INFO("APP", "Using 80% of screen resolution: " + std::to_string(width_) + "x" + std::to_string(height_)
                              + " (screen: " + std::to_string(mode->width) + "x" + std::to_string(mode->height) + ")");
                }
            }

            if (width_ <= 0 || height_ <= 0) {
                width_ = kFallbackWindowWidth;
                height_ = kFallbackWindowHeight;
                AWOW_LOG_INFO("APP", "Using fallback resolution: " + std::to_string(width_) + "x" + std::to_string(height_));
            }
        }

        cache.get().windowWidth = width_;
        cache.get().windowHeight = height_;
        cache.markDirty();
    }
}

Application::~Application() {
    SettingsCache::instance().flush();
}

void Application::setInitialSnapshot(const std::string& filepath) {
    initialSnapshotPath_ = filepath;
}

bool Application::importPendingSnapshot() {
    if (!singleInstance_) {
        return false;
    }

    const std::string pendingPath = singleInstance_->getAndClearPendingFile();
    if (!awow::files::isSnapshotPath(pendingPath)) {
        return false;
    }

    if (!runSnapshotImport(pendingPath)) {
        AWOW_LOG_ERROR("SNAPSHOT", "Failed to import snapshot from another instance: " + pendingPath);
        return false;
    }
    return true;
}

int Application::run() {
#if AWOWLAB_ENABLE_UPDATER
    installPendingUpdater();
#endif

    // Initialize UI Manager first (needed for landing page)
    uiManager_ = std::make_unique<UIManager>(width_, height_, title_);
    if (!uiManager_->initialize()) {
        AWOW_LOG_ERROR("UI", "Failed to initialize UI Manager - check GPU drivers and Vulkan support");
        return -1;
    }

    // Install single-instance window hook for receiving file paths from other instances
    if (singleInstance_) {
        singleInstance_->installWindowHook(uiManager_->getGlfwWindow());

        // Set up callback so UIManager can check for pending files during render loop
        uiManager_->setPendingFileChecker([this]() {
            return singleInstance_ && singleInstance_->hasPendingFile();
        });
    }

    // Register .awsnap file association on first run (HKCU, no admin required)
    if (!awow::FileAssociation::isRegistered()) {
        if (awow::FileAssociation::registerAssociation()) {
            AWOW_LOG_INFO("FILEASSOC", "Registered .awsnap file association");
        } else {
            AWOW_LOG_WARNING("FILEASSOC", "Failed to register .awsnap file association");
        }
    }

    // If initial snapshot was set via command line, load it directly (skip landing page)
    if (!initialSnapshotPath_.empty()) {
        if (runSnapshotImport(initialSnapshotPath_)) {
            initialSnapshotPath_.clear();
        } else {
            AWOW_LOG_ERROR("SNAPSHOT", "Failed to import initial snapshot: " + initialSnapshotPath_);
            initialSnapshotPath_.clear();
        }
    }

    // Create shared ResponseCache for WarcraftLogs (reused across flows)
    warcraftlogs::ResponseCache responseCache(kWclCacheDir);

    // Set when a WarcraftLogs flow failed on sign-in, so the landing page
    // reopens the sign-in dialog
    bool autoOpenWclDialog = false;
    const auto signInFailed = []() {
        return !awow::wclAuthError().empty() || !awow::wclNotice().empty();
    };

    // Validation callback for API credentials
    const auto validateCredentials = [](const std::string& clientId, const std::string& clientSecret, std::string& outError) -> bool {
        try {
            warcraftlogs::OAuthClient testClient(clientId, clientSecret);
            std::string token = testClient.getToken();
            return !token.empty();
        } catch (const std::exception& e) {
            outError = std::string("Authentication failed: ") + e.what();
            return false;
        }
    };

    // Each pass starts at the landing page
    while (true) {
        if (singleInstance_ && singleInstance_->hasPendingFile() && importPendingSnapshot()) {
            continue;  // Return to landing page after viewing
        }

        LandingPageResult landingResult = uiManager_->showLandingPage(
            validateCredentials, &responseCache, autoOpenWclDialog);
        autoOpenWclDialog = false;  // Reset flag after showing

        switch (landingResult.choice) {
            case LandingPageChoice::Exit:
                AWOW_LOG_INFO("APP", "User exited from landing page.");
                return 0;

            case LandingPageChoice::LocalFile: {
                AWOW_LOG_INFO("APP", "Opening file browser...");
                std::string filepath = FileDialog::openCombatLogFile();

                if (filepath.empty()) {
                    AWOW_LOG_INFO("APP", "No file selected. Returning to landing page.");
                    continue;  // Return to landing page
                }

                if (!loadCombatLog(filepath)) {
                    AWOW_LOG_ERROR("APP", "Failed to load combat log: " + filepath);
                    continue;  // Return to landing page
                }

                if (!runEncounterLoop()) {
                    return -1;
                }

                clearCombatLog();
                continue;
            }

            case LandingPageChoice::LocalPlayAgain: {
                // Play Along against a local log
                if (!runLocalPlayAgain()) {
                    AWOW_LOG_ERROR("PLAYAGAIN", "Play Again mode failed");
                }
                continue;
            }

            case LandingPageChoice::WarcraftLogs: {
                AWOW_LOG_INFO("WCL", "WarcraftLogs URL: " + landingResult.warcraftLogsUrl);
                if (!landingResult.accessToken.empty()) {
                    AWOW_LOG_INFO("WCL", "Using cached access token from dialog.");
                }

                if (!runWarcraftLogsFlow(landingResult.warcraftLogsUrl, landingResult.accessToken)) {
                    AWOW_LOG_ERROR("WCL", "WarcraftLogs flow failed for URL: " + landingResult.warcraftLogsUrl);
                    autoOpenWclDialog = signInFailed();
                }
                continue;
            }

            case LandingPageChoice::CachedFight: {
                AWOW_LOG_INFO("WCL", "Loading cached fight: " + landingResult.cachedReportCode
                          + " fight " + std::to_string(landingResult.cachedFightId));

                // Passing the fight id skips the fight selector
                if (!runWarcraftLogsFlowWithFight(landingResult.cachedReportCode,
                                                   landingResult.cachedFightId)) {
                    AWOW_LOG_ERROR("WCL", "Failed to load cached fight: " + landingResult.cachedReportCode + " fight " + std::to_string(landingResult.cachedFightId));
                    autoOpenWclDialog = signInFailed();
                }
                continue;
            }

            case LandingPageChoice::PlayAlong: {
                // Play Along: practise encounter positioning
                AWOW_LOG_INFO("PLAYALONG", "Loading cached fight: " + landingResult.cachedReportCode
                          + " fight " + std::to_string(landingResult.cachedFightId));

                if (!runPlayAlongMode(landingResult.cachedReportCode, landingResult.cachedFightId)) {
                    AWOW_LOG_ERROR("PLAYALONG", "Play Along mode failed for: " + landingResult.cachedReportCode + " fight " + std::to_string(landingResult.cachedFightId));
                    autoOpenWclDialog = signInFailed();
                }
                continue;
            }

            case LandingPageChoice::ImportSnapshot: {
                AWOW_LOG_INFO("SNAPSHOT", "Opening snapshot browser...");
                std::string filepath = FileDialog::openSnapshotFile();

                if (filepath.empty()) {
                    AWOW_LOG_INFO("SNAPSHOT", "No snapshot selected. Returning to landing page.");
                    continue;
                }

                if (!runSnapshotImport(filepath)) {
                    AWOW_LOG_ERROR("SNAPSHOT", "Failed to import snapshot: " + filepath);
                }
                continue;
            }

            case LandingPageChoice::LoadPendingFile: {
                // File received from another instance while on landing page
                importPendingSnapshot();
                continue;
            }

            case LandingPageChoice::OverlayMode: {
                AWOW_LOG_INFO("APP", "Launching overlay mode");
                runOverlayMode();
                continue;
            }

            case LandingPageChoice::PrepareMentorLocal: {
                // Same as LocalFile, but flagged so the encounter loop stops
                // at the player pick instead of opening the replay.
                AWOW_LOG_INFO("BENCHMARK", "Preparing a reference from a local log");
                std::string filepath = FileDialog::openCombatLogFile();
                if (filepath.empty()) continue;

                if (!loadCombatLog(filepath)) {
                    AWOW_LOG_ERROR("BENCHMARK", "Failed to load combat log: " + filepath);
                    continue;
                }

                auto& prepSession = benchmark::BenchmarkSession::instance();
                prepSession.setPrepareMentorPending(true);
                const bool loopOk = runEncounterLoop();
                prepSession.setPrepareMentorPending(false);
                clearCombatLog();
                if (!loopOk) {
                    return -1;
                }
                continue;
            }

            case LandingPageChoice::PrepareMentor: {
                // Load the chosen log and turn one of its players into a
                // reference. The flag is what separates this from opening the
                // same log as an ordinary replay.
                auto& prepSession = benchmark::BenchmarkSession::instance();
                prepSession.setPrepareMentorPending(true);
                AWOW_LOG_INFO("BENCHMARK",
                    "PrepareMentor chosen: report=" + landingResult.cachedReportCode +
                    " fight=" + std::to_string(landingResult.cachedFightId) +
                    " pendingFlag=" + (prepSession.isPrepareMentorPending() ? "true" : "false"));

                if (!runWarcraftLogsFlowWithFight(landingResult.cachedReportCode,
                                                  landingResult.cachedFightId)) {
                    AWOW_LOG_ERROR("BENCHMARK", "Prepare reference flow failed");
                    prepSession.setPrepareMentorPending(false);
                }
                continue;
            }

            case LandingPageChoice::CompareLive: {
                // The reference was already chosen in the picker, so live mode
                // starts armed and knows exactly what it is measuring against.
                AWOW_LOG_INFO("APP", "Launching live compare mode");
                auto& session = benchmark::BenchmarkSession::instance();
                if (!session.activateMentor({landingResult.benchmarkEncounterId,
                                             landingResult.benchmarkSpecId})) {
                    AWOW_LOG_ERROR("APP", "Chosen reference could not be loaded");
                    continue;
                }
                // Not the overlay: this mode is the report card, kept on the
                // newest pull. The live log is watched only to know when a
                // pull of that boss ended and the card's numbers should move.
                // The overlay is its own mode and stays that way.
                session.setLiveCompareArmed(true);
                runLiveCompareMode();
                session.setLiveCompareArmed(false);
                continue;
            }

            case LandingPageChoice::CompareReports: {
                // Reference already chosen; this loads the pull to measure
                // against it. Both are for the same encounter because the
                // picker only offered matching ones.
                AWOW_LOG_INFO("APP", "Launching report compare mode");
                auto& session = benchmark::BenchmarkSession::instance();
                if (!session.activateMentor({landingResult.benchmarkEncounterId,
                                             landingResult.benchmarkSpecId})) {
                    AWOW_LOG_ERROR("APP", "Chosen reference could not be loaded");
                    continue;
                }
                session.setReportComparePending(true);
                if (!runWarcraftLogsFlowWithFight(landingResult.cachedReportCode,
                                                  landingResult.cachedFightId)) {
                    AWOW_LOG_ERROR("WCL", "Report compare flow failed");
                }
                continue;
            }

            case LandingPageChoice::PackageForAnalysis: {
                if (!runPackageForAnalysis()) {
                    AWOW_LOG_ERROR("PACKAGE", "Analysis package was not written");
                }
                continue;
            }

            case LandingPageChoice::FetchRankingsDungeon:
            case LandingPageChoice::FetchRankingsRaid: {
                // Both land here now: the selector asks for content type along
                // with spec and hero talent, so the card no longer needs to.
                if (!runFetchRankings(
                        baseline_ui::BaselineContent::Dungeon, false)) {
                    AWOW_LOG_ERROR("RANKINGS", "Rankings were not fetched");
                }
                continue;
            }

            case LandingPageChoice::RebuildSeasonList: {
                // Refetches the season list itself, not any boss's rankings.
                // The content kind is unused on this path - the listing is one
                // file covering every kind - so it passes the default.
                if (!runFetchRankings(
                        baseline_ui::BaselineContent::Dungeon, true)) {
                    AWOW_LOG_ERROR("RANKINGS", "Season list was not rebuilt");
                }
                continue;
            }

            default:
                return 0;
        }
    }

    return 0;
}

bool Application::loadCombatLog(const std::string& filepath) {
    AWOW_LOG_INFO("APP", "Selected file: " + filepath);

    // Remembered so any reference built from this log can be re-cut later
    // without asking the user where it came from.
    benchmark::BenchmarkSession::instance().setLoadedSource(std::string(), filepath, 0);

    logSession_ = std::make_unique<LogSession>();
    if (!logSession_->open(filepath)) {
        AWOW_LOG_ERROR("APP", "Error opening combat log file: " + filepath);
        logSession_.reset();
        return false;
    }

    return true;
}

bool Application::runEncounterLoop() {
    if (!logSession_ || !logSession_->isOpen()) {
        AWOW_LOG_ERROR("APP", "No log session open - cannot run encounter loop");
        return false;
    }

    bool continueRunning = true;

    while (continueRunning) {
        std::vector<std::vector<std::string_view>> linesToParse;
        std::string encounterName;

        const auto& encounters = logSession_->getEncounters();

        if (encounters.empty()) {
            AWOW_LOG_INFO("APP", "No encounters found. Loading full log.");
            // On-demand: tokenize the full file only when needed
            linesToParse = logSession_->getAllLines();
        } else {
            bool selectionMade = uiManager_->showEncounterSelectorWithRefresh(
                encounters,
                logSession_->getTotalLines(),
                logSession_->getScannedBytes(),
                logSession_->getTotalBytes(),
                [this]() -> bool {
                    AWOW_LOG_INFO("APP", "Refreshing log...");
                    bool hasNew = logSession_->refresh();
                    if (hasNew) {
                        AWOW_LOG_INFO("APP", "Found new content!");
                    }
                    return hasNew;
                }
            );

            if (!selectionMade) {
                AWOW_LOG_INFO("APP", "No selection made. Returning to landing page.");
                return true;
            }

            size_t selectedIndex = uiManager_->getSelectedEncounterIndex();

            // Re-fetch encounters in case refresh happened
            const auto& currentEncounters = logSession_->getEncounters();

            if (selectedIndex >= currentEncounters.size()) {
                AWOW_LOG_ERROR("APP", "Invalid encounter index: " + std::to_string(selectedIndex) + " (max: " + std::to_string(currentEncounters.size()) + ")");
                continue;
            }
            const auto& selectedEncounter = currentEncounters[selectedIndex];
            AWOW_LOG_INFO("APP", "Loading encounter: " + selectedEncounter.encounterName);
            publishCurrentEncounter(selectedEncounter);

            // On-demand: tokenize only the selected encounter's byte range
            linesToParse = logSession_->getEncounterLines(selectedIndex);
            encounterName = selectedEncounter.encounterName
                          + " (" + getDifficultyName(selectedEncounter.difficultyId) + ")";

            uiManager_->setWindowTitle("AwowLab - " + encounterName);
        }

        ExtractedData extractedData;
        {
            AWOW_PERF_SCOPE("OutputWriter::extractAllEvents");
            extractedData = OutputWriter::extractAllEvents(linesToParse);
        }

        AWOW_LOG_INFO("APP", "Parsed " + std::to_string(extractedData.actorEvents.size()) + " position events, "
                  + std::to_string(extractedData.spellCastEvents.size()) + " spell cast events, "
                  + std::to_string(extractedData.resourceEvents.size()) + " resource events, "
                  + std::to_string(extractedData.combatantInfoEvents.size()) + " combatant info events, "
                  + std::to_string(extractedData.mapChangeEvents.size()) + " map change events");

#ifndef NDEBUG
        // Written before the mapping is released: the events still point into it
        std::string csvOutputPath = "debug_parsed_positions.csv";
        if (OutputWriter::writeToCSV(extractedData.actorEvents, csvOutputPath)) {
            AWOW_LOG_INFO("APP", "Debug CSV written to: " + csvOutputPath);
        }
#endif

        // Preparing a reference from a local log. Unlike the imported path
        // there is no metadata roster to ask from first, so the roster comes
        // from the events that were just parsed.
        if (benchmark::BenchmarkSession::instance().isPrepareMentorPending()) {
            auto& benchSession = benchmark::BenchmarkSession::instance();

            std::vector<PlayAlong::PlayerInfo> roster;
            for (const auto& info : extractedData.combatantInfoEvents) {
                if (info.player_guid.empty()) continue;
                // One entry per player: a local log repeats COMBATANT_INFO at
                // every encounter start.
                const bool seen = std::any_of(
                    roster.begin(), roster.end(),
                    [&info](const PlayAlong::PlayerInfo& p) {
                        return p.guid == info.player_guid;
                    });
                if (seen) continue;

                PlayAlong::PlayerInfo player;
                player.guid = info.player_guid;
                player.specId = info.spec_id;
                // Local logs carry the spec id itself, so the class comes from
                // the same table the report path resolves against.
                if (info.spec_id != 0) {
                    player.className = parser::getClassName(info.spec_id);
                }
                if (auto it = extractedData.guidToNameMap.find(info.player_guid);
                    it != extractedData.guidToNameMap.end()) {
                    player.name = it->second;
                }
                if (player.name.empty()) player.name = info.player_guid;
                roster.push_back(std::move(player));
            }

            AWOW_LOG_INFO("BENCHMARK",
                "Prepare (local): offering " + std::to_string(roster.size()) + " players");

            const auto picked = roster.empty()
                ? PlayAlong::PlayerSelection{}
                : uiManager_->pickBenchmarkPlayer(roster);

            if (picked.selected) {
                prepareMentorFrom(extractedData, picked.playerGuid,
                                  picked.playerName, picked.specId);
            }

            benchSession.setPrepareMentorPending(false);
            linesToParse.clear();
            logSession_->releaseFileMapping();
            return true;   // Back to the landing page, never into the replay
        }

        EncounterDataLoader loader;
        loader.setPerformanceMonitoring(true);
#ifndef NDEBUG
        loader.setDebugDumping(true, "loc_");
#endif
        DatabaseBundle bundle = loader.loadFromExtractedData(extractedData);

        // The databases own their strings now, so the mapping can go
        linesToParse.clear();
        logSession_->releaseFileMapping();

        if (!bundle.isValid()) {
            AWOW_LOG_ERROR("APP", "Failed to create database bundle");
            continue;
        }

        loader.loadIntoUI(uiManager_.get(), bundle, extractedData, CurrentEncounter::instance().getId());

        uiManager_->setExportCallback(exportSnapshot);

        uiManager_->resetNavigationFlags();
        uiManager_->run();

        if (uiManager_->shouldReturnToLandingPage()) {
            uiManager_->unloadDatabases();
            AWOW_LOG_INFO("APP", "Returning to landing page...");
            return true;  // Signal to go back to landing page
        } else if (uiManager_->shouldRefreshEncounters()) {
            // F5: refresh the log and go back to the encounter selector
            uiManager_->unloadDatabases();
            uiManager_->resetRefreshFlag();
            AWOW_LOG_INFO("APP", "Refreshing encounters (F5)...");
            logSession_->refresh();
            AWOW_LOG_INFO("APP", "Found " + std::to_string(logSession_->getEncounters().size()) + " encounter(s).");
        } else if (uiManager_->shouldReturnToEncounterSelector()) {
            // Unload databases before they go out of scope
            uiManager_->unloadDatabases();
            AWOW_LOG_INFO("APP", "Returning to encounter selector...");
        } else {
            continueRunning = false;
        }
    }

    return true;
}

void Application::clearCombatLog() {
    logSession_.reset();

    uiManager_->setWindowTitle("AwowLab - Combat Log Visualizer");

    AWOW_LOG_INFO("APP", "Combat log data cleared.");
}

bool Application::runWarcraftLogsFlow(const std::string& urlOrCode, const std::string& cachedAccessToken) {
    using namespace warcraftlogs;

    wclSession_ = std::make_unique<WarcraftLogsSession>();
    wclSession_->setCacheEnabled(true, kWclCacheDir);

    // Loaded before any fight data: the WCL adapter matches talents against it
    auto& modifierRegistry = awow::CooldownModifierRegistry::instance();
    {
        std::string spellDataVersion = SettingsCache::instance().get().spellDataVersion;
        std::string modifierPath = awow::getExecutableDirectory() +
            "data\\cooldown_modifiers_" + spellDataVersion + ".json";
        if (!modifierRegistry.loadFromJson(modifierPath)) {
            AWOW_LOG_WARNING("DATA", "Failed to load talent CD modifiers from: " + modifierPath + " - using base cooldown values");
        }
    }

    AWOW_LOG_INFO("WCL", "Parsing WarcraftLogs URL...");
    if (!wclSession_->initialize(urlOrCode)) {
        AWOW_LOG_ERROR("WCL", "Failed to parse URL '" + urlOrCode + "': " + wclSession_->getLastError());
        wclSession_.reset();
        return false;
    }

    AWOW_LOG_INFO("WCL", "Report code: " + wclSession_->getReportCode());

    // If we have a cached access token from the dialog, inject it into the session
    // Token will be used if an API call is needed (cache miss)
    if (!cachedAccessToken.empty()) {
        AWOW_LOG_INFO("WCL", "Injecting cached access token into session.");
        wclSession_->setUserAccessToken(cachedAccessToken);
    }

    // No sign-in check up front. Auth happens only when a cache miss needs
    // the API, so cached fights load without it.

    awow::wclNotice().clear();
    AWOW_LOG_INFO("WCL", "Fetching report metadata...");
    if (!wclSession_->fetchReport([this](const std::string& msg, float progress) {
        uiManager_->renderProgressFrame(msg, progress);
    })) {
        std::string error = wclSession_->getLastError();
        AWOW_LOG_ERROR("WCL", "Failed to fetch report for code '" + wclSession_->getReportCode() + "': " + error);
        if (isUserPointsError(error)) {
            noteUserPointsSpent(*wclSession_);
        } else {
            persistIfAuthError(error);
        }
        wclSession_.reset();
        return false;
    }

    const auto& fights = wclSession_->getFights();
    AWOW_LOG_INFO("WCL", "Found " + std::to_string(fights.size()) + " fights in report.");

    if (fights.empty()) {
        AWOW_LOG_ERROR("WCL", "No fights found in report: " + wclSession_->getReportCode());
        wclSession_.reset();
        return false;
    }

    auto requestedFightId = wclSession_->getRequestedFightId();
    int32_t selectedFightId = -1;

    if (requestedFightId.has_value()) {
        selectedFightId = requestedFightId.value();
        AWOW_LOG_INFO("WCL", "Using fight from URL: " + std::to_string(selectedFightId));
    } else {
        selectedFightId = uiManager_->showFightSelector(wclSession_.get());

        if (selectedFightId < 0) {
            AWOW_LOG_INFO("WCL", "Fight selection cancelled.");
            wclSession_.reset();
            return false;
        }
    }

    if (selectedFightId < 0) {
        AWOW_LOG_ERROR("WCL", "No fight selected for report: " + wclSession_->getReportCode());
        wclSession_.reset();
        return false;
    }

    // Find fight info for window title and CurrentEncounter
    std::string fightName = "Unknown Fight";
    int32_t encounterId = 0;
    uint16_t difficulty = 0;
    int32_t durationMs = 0;
    for (const auto& fight : fights) {
        if (fight.id == selectedFightId) {
            fightName = fight.name;
            encounterId = fight.encounterID;
            difficulty = static_cast<uint16_t>(fight.difficulty);
            durationMs = static_cast<int32_t>(fight.duration());
            break;
        }
    }

    // Global encounter info, read by trigger persistence and snapshot export
    // Ensures annotations transfer between WarcraftLogs and local logs for same encounter
    CurrentEncounter::instance().set(
        fightName,
        encounterId,
        difficulty,
        0,  // mapId will be derived from mapChangeEvents during export
        durationMs
    );

    uiManager_->setWindowTitle("AwowLab - " + fightName + " (WarcraftLogs)");

    // Both benchmark flows ask who to profile before the events are fetched.
    // The roster is already in the report metadata, so picking costs nothing;
    // cancelling here avoids downloading and parsing a fight nobody wanted.
    std::string prepareGuid;
    std::string prepareName;
    uint16_t prepareSpecId = 0;
    bool comparingAgainstReference = false;

    if (benchmark::BenchmarkSession::instance().isPrepareMentorPending() ||
        benchmark::BenchmarkSession::instance().isReportComparePending()) {

        comparingAgainstReference =
            benchmark::BenchmarkSession::instance().isReportComparePending();
        std::vector<PlayAlong::PlayerInfo> roster;
        if (const auto* metadata = wclSession_->getReportMetadata()) {
            for (const auto& actor : metadata->actors) {
                if (actor.actorType != warcraftlogs::WclActorType::Player) continue;
                PlayAlong::PlayerInfo player;
                player.guid = actor.guid;
                player.name = actor.name.empty() ? actor.guid : actor.name;
                player.specId = actor.specId;
                player.className = actor.className;
                roster.push_back(std::move(player));
            }
        }

        // Comparing narrows to the reference's class, not its spec. Report
        // metadata names the class for every player but only carries a spec
        // for some - a bare "DemonHunter" icon with no suffix is common - so
        // filtering on spec dropped players who do play it and simply were not
        // labelled. Class is the strongest filter the metadata can support,
        // and a wrong-spec pick is caught by the engine rather than silently
        // compared.
        auto& benchSession = benchmark::BenchmarkSession::instance();
        if (comparingAgainstReference) {
            const std::string wantedClass =
                parser::getClassName(benchSession.specId());

            if (!wantedClass.empty() && wantedClass != "Unknown") {
                const auto squash = [](std::string text) {
                    text.erase(std::remove_if(text.begin(), text.end(),
                                              [](unsigned char c) {
                                                  return std::isspace(c) != 0;
                                              }),
                               text.end());
                    std::transform(text.begin(), text.end(), text.begin(),
                                   [](unsigned char c) {
                                       return static_cast<char>(std::tolower(c));
                                   });
                    return text;
                };

                // "Demon Hunter" from the table vs "DemonHunter" from the
                // report - same class, different spacing.
                const std::string wanted = squash(wantedClass);
                roster.erase(std::remove_if(roster.begin(), roster.end(),
                                            [&](const PlayAlong::PlayerInfo& p) {
                                                return squash(p.className) != wanted;
                                            }),
                             roster.end());
            }
        }

        AWOW_LOG_INFO("BENCHMARK",
            std::string(comparingAgainstReference ? "Compare" : "Prepare") +
            ": offering " + std::to_string(roster.size()) + " players from metadata");

        const auto clearPending = [&benchSession]() {
            benchSession.setPrepareMentorPending(false);
            benchSession.setReportComparePending(false);
        };

        if (roster.empty()) {
            AWOW_LOG_ERROR("BENCHMARK",
                comparingAgainstReference
                    ? "Nobody in this report plays the reference's spec"
                    : "No players in this report's metadata");
            clearPending();
            wclSession_.reset();
            return false;
        }

        const auto picked = uiManager_->pickBenchmarkPlayer(roster);
        if (!picked.selected) {
            AWOW_LOG_INFO("BENCHMARK", "Cancelled before loading the fight");
            clearPending();
            wclSession_.reset();
            return true;
        }

        prepareGuid = picked.playerGuid;
        prepareName = picked.playerName;
        prepareSpecId = picked.specId;
        AWOW_LOG_INFO("BENCHMARK",
            prepareName + " picked, loading their events");
    }

    AWOW_LOG_INFO("WCL", "Loading fight " + std::to_string(selectedFightId) + " (" + fightName + ")...");
    benchmark::BenchmarkSession::instance().setLoadedSource(
        wclSession_ ? wclSession_->getReportCode() : std::string(),
        std::string(), selectedFightId);

    ExtractedData extractedData;
    try {
        extractedData = wclSession_->loadFight(selectedFightId, [this, &fightName](const std::string& /*msg*/, float progress) {
            uiManager_->renderProgressFrame("Loading " + fightName, progress);
        });
    } catch (const std::exception& e) {
        std::string error = e.what();
        AWOW_LOG_ERROR("WCL", "Failed to load fight " + std::to_string(selectedFightId) + " from " + wclSession_->getReportCode() + ": " + error);
        if (isUserPointsError(error)) {
            noteUserPointsSpent(*wclSession_);
        } else {
            persistIfAuthError(error);
        }
        wclSession_.reset();
        return false;
    }

    AWOW_LOG_INFO("WCL", "Parsed " + std::to_string(extractedData.actorEvents.size()) + " position events, "
              + std::to_string(extractedData.spellCastEvents.size()) + " spell cast events, "
              + std::to_string(extractedData.resourceEvents.size()) + " resource events");

    // Preparing a reference wants the events, not the replay. Building the
    // databases and entering the render loop would be work nobody asked for,
    // and it drops the user into a 3D view when all they wanted was to pick a
    // player and classify some spells.
    if (!prepareGuid.empty()) {
        auto& benchSession = benchmark::BenchmarkSession::instance();

        if (comparingAgainstReference) {
            // Measure this pull against the reference and show the result.
            // Never opens the replay: the report card is the answer, and the
            // replay is something the user opens from it if they want it.
            AWOW_LOG_INFO("BENCHMARK", "Comparing " + prepareName + " to the reference");

            const auto& result = benchSession.compareAgainst(
                extractedData, prepareGuid, prepareName);

            if (!result.valid) {
                AWOW_LOG_INFO("BENCHMARK",
                    "Comparison blocked: " + result.blockingReason);

                // Unconfirmed classifications are the one blocker the user can
                // clear right here, so offer that instead of a dead end.
                if (result.blockingReason == "unconfirmed_spells") {
                    uiManager_->runClassificationReview(benchSession.specId(),
                                                        benchSession.spellNames());
                    // The user's side is re-extracted, not just re-scored -
                    // the rulings that just landed decide what its numbers
                    // even mean, and the events are right here. The reference
                    // was built on an earlier night from a log this flow does
                    // not have; if its rulings moved too, the card says so
                    // and offers a rebuild.
                    benchSession.compareAgainst(extractedData, prepareGuid,
                                                prepareName);
                }
            }

            AWOW_LOG_INFO("BENCHMARK",
                std::string("Comparison ") +
                (benchSession.result().valid ? "ready - showing the report card"
                                             : "could not run"));

            showBenchmarkReport();

            benchSession.setReportComparePending(false);
            wclSession_.reset();
            return true;
        }

        AWOW_LOG_INFO("BENCHMARK", "Building reference for " + prepareName);

        // The origin comes from setLoadedSource above, which recorded the
        // report code and fight id when this log was opened.
        prepareMentorFrom(extractedData, prepareGuid, prepareName, prepareSpecId);

        benchSession.setPrepareMentorPending(false);
        wclSession_.reset();
        return true;
    }

    EncounterDataLoader loader;
    loader.setPerformanceMonitoring(true);
#ifndef NDEBUG
    loader.setDebugDumping(true, "wcl_");
#endif
    DatabaseBundle bundle = loader.loadFromExtractedData(extractedData);

    if (!bundle.isValid()) {
        AWOW_LOG_ERROR("WCL", "Failed to create database bundle");
        wclSession_.reset();
        return false;
    }

    loader.loadIntoUI(uiManager_.get(), bundle, extractedData, CurrentEncounter::instance().getId());

    uiManager_->setExportCallback(exportSnapshot);

    uiManager_->resetNavigationFlags();
    uiManager_->run();

    uiManager_->unloadDatabases();
    wclSession_.reset();

    AWOW_LOG_INFO("WCL", "Returning to landing page...");
    return true;
}

bool Application::loadMentorSourceEvents(ExtractedData& data) {
    auto& benchSession = benchmark::BenchmarkSession::instance();
    const auto origin = benchSession.mentorOrigin();
    if (!origin.isValid()) {
        AWOW_LOG_WARNING("BENCHMARK",
            "The reference has no recorded source to reopen");
        return false;
    }

    if (!origin.reportCode.empty()) {
        // Cached reports only. A rebuild is a consequence of the user changing
        // a spell role, and silently spending API budget on it is not a cost
        // they asked for.
        //
        // A session of its own: wclSession_ is open on whatever report the
        // user is looking at now, which need not be the reference's. It is
        // given no access token, so a cache miss fails instead of fetching.
        warcraftlogs::WarcraftLogsSession session;
        session.setCacheEnabled(true, kWclCacheDir);
        if (!session.initialize(origin.reportCode) || !session.fetchReport()) {
            AWOW_LOG_WARNING("BENCHMARK",
                "The reference's report is not cached: " + session.getLastError());
            return false;
        }
        try {
            data = session.loadFight(origin.fightId, nullptr);
        } catch (const std::exception& e) {
            AWOW_LOG_ERROR("BENCHMARK",
                std::string("Could not reload the report: ") + e.what());
            return false;
        }
    } else {
        LogSession session;
        if (!session.open(origin.logPath)) {
            AWOW_LOG_ERROR("BENCHMARK",
                "Could not reopen " + origin.logPath);
            return false;
        }

        const auto* profile = benchSession.mentorProfile();
        if (!profile) return false;

        // Found by boss id, not by the index recorded at prepare time: a log
        // that has grown since then has different indices, and rebuilding
        // against the wrong pull would be worse than not rebuilding at all.
        // Benchmarking is raid-only for now, so the id identifies the fight.
        const auto& encounters = session.getEncounters();
        size_t index = encounters.size();
        for (size_t i = 0; i < encounters.size(); ++i) {
            if (encounters[i].encounterId == profile->encounterId) {
                index = i;
                break;
            }
        }
        if (index == encounters.size()) {
            AWOW_LOG_ERROR("BENCHMARK",
                "Could not find the original encounter in " + origin.logPath);
            return false;
        }

        data = OutputWriter::extractAllEvents(session.getEncounterLines(index));
    }

    return true;
}

bool Application::reopenMentorPrepare() {
    auto& benchSession = benchmark::BenchmarkSession::instance();

    const auto* profile = benchSession.mentorProfile();
    if (!profile) return false;

    // Captured before the reload: prepareMentorFrom re-cuts the profile, and
    // reading these back off it afterwards would read the new one.
    const std::string playerGuid = profile->origin.actorGuid;
    const std::string playerName = profile->playerName;
    const uint16_t specId = profile->specId;

    ExtractedData data;
    if (!loadMentorSourceEvents(data)) {
        return false;
    }

    // Straight back into the same three steps, against the mentor's own log
    // rather than whatever happens to be loaded. prepareMentorFrom re-cuts on
    // the way out, so a role, phase or weight changed here lands in the
    // numbers before the preview reappears.
    prepareMentorFrom(data, playerGuid, playerName, specId);
    return true;
}

void Application::prepareMentorFrom(const ExtractedData& data,
                                    const std::string& playerGuid,
                                    const std::string& playerName,
                                    uint16_t specId) {
    auto& benchSession = benchmark::BenchmarkSession::instance();

    // The prepare flow stops before EncounterDataLoader runs, so nothing else
    // hands the session these events - and without them the phase step has no
    // spells or emotes to offer, leaving fixed-time splits as the only option.
    benchSession.setLiveExtractedData(&data);

    const size_t pending = benchSession.setMentor(
        data, playerGuid, playerName, specId, std::string(),
        CurrentEncounter::instance().getName(),
        CurrentEncounter::instance().getId(),
        CurrentEncounter::instance().getDifficulty());

    AWOW_LOG_INFO("BENCHMARK",
        "Reference built - " + std::to_string(pending) + " spells to classify");

    // Spell roles, phases and mob weights all shape what the extractor
    // produces, so all three are settled before the reference is kept. The
    // profile above exists only to generate the suggestions the first step
    // rules on; it is re-cut below once those answers are in.
    if (!uiManager_->runPrepareWizard(specId, playerName,
                                      benchSession.spellNames())) {
        // Cancelled: leave nothing half-prepared behind, and drop the events
        // pointer before returning past the clear at the end.
        benchSession.clear();
        benchSession.setLiveExtractedData(nullptr);
        AWOW_LOG_INFO("BENCHMARK", "Prepare cancelled");
        return;
    }

    if (benchSession.refreshMentorAfterReview(data)) {
        AWOW_LOG_INFO("BENCHMARK", "Reference cut with the chosen settings");
    }

    // Show what was built, so a broken reference is caught now rather than
    // in a confusing comparison later. "Change prepared log" reopens the
    // three steps against the log still in hand and re-cuts, so a changed
    // spell role, phase boundary or mob weight shows up straight away.
    while (uiManager_->runMentorPreview(benchSession.spellNames())) {
        if (!uiManager_->runPrepareWizard(specId, playerName,
                                          benchSession.spellNames())) {
            break;   // Cancelled out of the settings; keep what was built.
        }
        if (benchSession.refreshMentorAfterReview(data)) {
            AWOW_LOG_INFO("BENCHMARK", "Reference re-cut after settings change");
        }
    }

    // These events belong to the caller and are about to go out of scope, so
    // the session must not keep pointing at them.
    benchSession.setLiveExtractedData(nullptr);
}

void Application::showBenchmarkReport() {
    uiManager_->runBenchmarkReport();

    // The card closed asking to go back to the reference. Done here rather
    // than in the panel: the mentor's own log has to be re-opened, and only
    // this layer can reach a file or a report.
    //
    // Opens the prepare steps rather than re-cutting silently, because the
    // reason to come back is rarely just a spell role - a phase boundary or a
    // mob weight is as likely, and all three live in those steps.
    auto& benchSession = benchmark::BenchmarkSession::instance();
    if (benchSession.isMentorRebuildRequested()) {
        benchSession.clearMentorRebuildRequest();
        if (!reopenMentorPrepare()) {
            // Both paths read the same origin, so there is nothing to fall
            // back to - the profile predates origins being recorded, or the
            // report is no longer cached. loadMentorSourceEvents logged why.
            AWOW_LOG_WARNING("BENCHMARK",
                "Could not reopen the reference; it is unchanged");
        }
    }
}

bool Application::runWarcraftLogsFlowWithFight(const std::string& reportCode, int32_t fightId) {
    const std::string url = reportFightUrl(reportCode, fightId);
    AWOW_LOG_INFO("WCL", "Loading cached fight via URL: " + url);
    return runWarcraftLogsFlow(url, storedWclAccessToken());
}

namespace {

// What a content choice means to the parts that do not know about the picker.
//
// Dummy is not a variant of the other two. Its configurations are separate
// encounters with their own rankings, so it is a third source of samples, and
// the difficulty it offers is Normal because a dummy has no difficulty to
// speak of.
warcraftlogs::ZoneKind zoneKindFor(baseline_ui::BaselineContent content) {
    switch (content) {
        case baseline_ui::BaselineContent::Raid:  return warcraftlogs::ZoneKind::Raid;
        case baseline_ui::BaselineContent::Dummy: return warcraftlogs::ZoneKind::Dummy;
        case baseline_ui::BaselineContent::Dungeon:
        default:                                  return warcraftlogs::ZoneKind::Dungeon;
    }
}

// WCL's difficulty ids: Mythic for raids, the single "Dungeon" for keystones,
// Normal for a dummy zone, which offers nothing else.
int32_t difficultyFor(baseline_ui::BaselineContent content) {
    switch (content) {
        case baseline_ui::BaselineContent::Raid:  return 5;
        case baseline_ui::BaselineContent::Dummy: return 3;
        case baseline_ui::BaselineContent::Dungeon:
        default:                                  return 10;
    }
}

// Recorded in the manifest so a reader knows which population the package was
// drawn from. A dummy sample and an encounter sample answer different
// questions and must not be pooled, so the stratum has to distinguish them.
const char* contentStratum(baseline_ui::BaselineContent content) {
    switch (content) {
        case baseline_ui::BaselineContent::Raid:  return "raid";
        case baseline_ui::BaselineContent::Dummy: return "dummy";
        case baseline_ui::BaselineContent::Dungeon:
        default:                                  return "dungeon";
    }
}

// What the things pulled are called, for the status line.
const char* contentUnit(baseline_ui::BaselineContent content) {
    switch (content) {
        case baseline_ui::BaselineContent::Raid:  return "bosses";
        case baseline_ui::BaselineContent::Dummy: return "dummy configurations";
        case baseline_ui::BaselineContent::Dungeon:
        default:                                  return "dungeons";
    }
}

} // namespace

bool Application::buildBaselineFromSample(
    const warcraftlogs::RankingSample& sample,
    const baseline_ui::SpecSelection& picked,
    const std::string& zoneName
) {
    // How many ranked pulls to actually fetch. Each one costs a query and the
    // rankings hand back hundreds, so this takes the top few per encounter:
    // enough to see how the spec is played, without spending an hour of the
    // rate limit on evidence that repeats itself.
    constexpr size_t kPullsPerEncounter = 3;

    std::vector<const warcraftlogs::RankedPull*> chosen;
    std::unordered_map<int32_t, size_t> takenPerEncounter;
    for (const auto& pull : sample.pulls) {
        auto& taken = takenPerEncounter[pull.encounterId];
        if (taken >= kPullsPerEncounter) continue;
        ++taken;
        chosen.push_back(&pull);
    }
    if (chosen.empty()) return false;

    const std::string outputDir = FileDialog::selectPackageFolder();
    if (outputDir.empty()) {
        AWOW_LOG_INFO("BASELINE", "No output folder chosen.");
        return true;   // cancelled, not failed
    }

    namespace pkg = awow::analysis;
    pkg::PackageRequest request;
    request.type = pkg::PackageType::BaselineSample;
    request.source = pkg::PackageSource::Wcl;
    request.notes = picked.className + " " + picked.specName
                  + (picked.heroTree.empty() ? "" : " / " + picked.heroTree)
                  + " baseline from " + zoneName;

    // The manifest records how the fights were picked, which is the only place
    // that survives - the rankings themselves are a selection step, not part of
    // the package.
    request.sampling.present = true;
    request.sampling.rankingMetric = "dps";
    request.sampling.stratum = contentStratum(picked.content);

    auto& spellDb = SpellNameDatabase::instance();
    request.resolveSpellName = shippedSpellNameResolver();

    // Each fight has to stay alive until the package is written, since FightData
    // holds a pointer rather than a copy.
    std::vector<ExtractedData> fetched;
    fetched.reserve(chosen.size());
    std::vector<pkg::FightData> fights;

    size_t index = 0;
    for (const auto* pull : chosen) {
        ++index;
        AWOW_LOG_INFO("BASELINE", "Fetching " + std::to_string(index) + " of "
                                      + std::to_string(chosen.size()) + ": "
                                      + pull->encounterName + " ("
                                      + pull->playerName + ")");

        if (!wclSession_ || !wclSession_->initialize(pull->reportCode)) {
            AWOW_LOG_WARNING("BASELINE", "Could not open report " + pull->reportCode);
            continue;
        }
        if (!wclSession_->fetchReport()) {
            AWOW_LOG_WARNING("BASELINE", "Could not read report " + pull->reportCode);
            continue;
        }

        // Find the ranked player among the report's actors so the fetch can be
        // narrowed to them. Without this the whole raid comes back - around
        // forty pages, one query each, to keep one player's events.
        int32_t actorId = 0;
        // The difficulty the fight was actually fought at. The rankings query
        // asked for one, but the report says what it got, which is the value
        // that belongs in the manifest - a package with difficulty 0 reads as
        // though the tier were unknown.
        //
        // Translated here because WCL numbers difficulties its own way and the
        // numbers overlap with WoW's: WCL's 5 is Mythic, WoW's 5 is 10-player
        // Heroic. Converting at the boundary keeps one scale inside the app,
        // which is what the rest of it assumes - getDifficultyName included.
        uint32_t fightDifficulty = 0;
        if (const auto* metadata = wclSession_->getReportMetadata()) {
            for (const auto& actor : metadata->actors) {
                if (actor.name == pull->playerName) { actorId = actor.id; break; }
            }
            for (const auto& fight : metadata->fights) {
                if (fight.id == pull->fightId) {
                    fightDifficulty =
                        warcraftlogs::wclDifficultyToWow(fight.difficulty);
                    break;
                }
            }
        }
        if (actorId == 0) {
            AWOW_LOG_WARNING("BASELINE", pull->playerName
                                             + " is not in that report; skipping");
            continue;
        }

        ExtractedData data = wclSession_->loadFight(pull->fightId, nullptr, actorId);
        if (data.combatEvents.empty()) {
            AWOW_LOG_WARNING("BASELINE", "Fight " + std::to_string(pull->fightId)
                                             + " had no events");
            continue;
        }

        std::string playerGuid;
        for (const auto& [guid, name] : data.guidToNameMap) {
            if (name == pull->playerName) { playerGuid = guid; break; }
        }

        fetched.push_back(std::move(data));
        const ExtractedData& stored = fetched.back();

        spellDb.loadSpellIds(stored.collectAllSpellIds());

        pkg::FightData fight;
        fight.data = &stored;
        fight.info.fightKey = std::to_string(index) + "_"
                            + std::to_string(pull->encounterId);
        fight.info.sourceName = pull->reportCode;
        fight.info.fightId = static_cast<uint32_t>(pull->fightId);
        fight.info.encounterId = static_cast<uint32_t>(pull->encounterId);
        fight.info.encounterName = pull->encounterName;
        fight.info.difficultyId = fightDifficulty;
        fight.info.kill = true;          // a ranked pull is a kill by definition
        fight.info.durationMs = static_cast<int32_t>(pull->durationMs);
        fight.info.playerGuid = playerGuid;
        fight.info.playerName = pull->playerName;
        fight.info.specId = picked.specId;
        fight.info.itemLevel = static_cast<uint32_t>(pull->itemLevel);
        fights.push_back(std::move(fight));
    }

    if (fights.empty()) {
        AWOW_LOG_ERROR("BASELINE", "No fights could be fetched");
        if (uiManager_) uiManager_->setLandingStatus("No fights could be fetched", true);
        return false;
    }

    const auto result = pkg::buildPackage(outputDir, request, fights);
    if (!result.ok) {
        AWOW_LOG_ERROR("BASELINE", "Package not written: " + result.error);
        if (uiManager_) uiManager_->setLandingStatus(result.error, true);
        return false;
    }

    const std::string done = "Baseline ready: " + std::to_string(fights.size())
                           + " fights, " + std::to_string(result.fileCount)
                           + " files";
    AWOW_LOG_INFO("BASELINE", done + " -> " + result.zipPath);
    if (uiManager_) uiManager_->setLandingStatus(done);
    return true;
}

bool Application::runFetchRankings(baseline_ui::BaselineContent content,
                                   bool rebuildOnly) {
    // Hero talent names come from the shipped file; without them the picker
    // cannot offer the choice at all. Loading is idempotent.
    awow::HeroTalents::instance().load(awow::getExecutableDirectory() + "data");

    baseline_ui::SpecSelection picked;
    if (!rebuildOnly) {
        // Spec first: it decides everything after it. Rankings are queried per
        // class and spec, and which hero trees exist depends on the spec.
        picked = uiManager_->pickBaselineSpec();
        if (!picked.selected) {
            AWOW_LOG_INFO("RANKINGS", "No spec picked. Returning to landing page.");
            return true;
        }
        content = picked.content;
        AWOW_LOG_INFO("RANKINGS", "Baseline for " + picked.className + " "
                                      + picked.specName
                                      + (picked.heroTree.empty()
                                             ? " (every hero tree)"
                                             : " / " + picked.heroTree));
    }

    // Rankings belong to the season, not to anything the user has logged, so
    // this needs no combat log and no cached report.
    if (!wclSession_) {
        wclSession_ = std::make_unique<warcraftlogs::WarcraftLogsSession>();
        wclSession_->setCacheEnabled(true, kWclCacheDir);
    }

    // The stored token, same as every other WCL path. Opening a browser sign-in
    // on each click would be asking the user to re-authorise something they
    // already authorised.
    const std::string cachedToken = storedWclAccessToken();
    if (!cachedToken.empty()) {
        wclSession_->setUserAccessToken(cachedToken);
    }

    // Beside the report cache: same lifetime, same reason to survive a
    // restart, and it is WCL data like everything else in there.
    const auto cachePath =
        std::filesystem::path("wcl_cache") / "season_zones.json";

    std::string zonesJson;
    bool wasFresh = false;
    const std::string error = wclSession_->loadWorldZones(
        cachePath.string(), rebuildOnly, zonesJson, &wasFresh);
    if (!error.empty()) {
        // An expired or missing token shows up here as a failed request rather
        // than as a sign-in prompt, so say which one it was.
        const bool authProblem =
            error.find("401") != std::string::npos ||
            error.find("Unauthorized") != std::string::npos ||
            error.find("not signed in") != std::string::npos ||
            error.find("token") != std::string::npos;

        const std::string message =
            authProblem ? "Sign in to WarcraftLogs first - use Fetch from URL once"
                        : error;
        AWOW_LOG_ERROR("RANKINGS", error);
        if (uiManager_) uiManager_->setLandingStatus(message, true);
        return false;
    }

    // The listing carries every expansion WCL has ever tracked, and several
    // copies of each season - PTR, beta, and the frozen previous one - plus
    // zones that are not bosses at all. Zones are sorted by shape rather than
    // by name; see parseSeasonZones.
    const auto zones = warcraftlogs::parseSeasonZones(zonesJson, zoneKindFor(content));

    if (rebuildOnly) {
        // Say it was skipped rather than claiming a fetch that did not happen.
        // Someone who just changed patch needs to know the old list is still in
        // use, not be told it was rebuilt.
        const std::string message =
            wasFresh
                ? std::string("Season list is already up to date - rebuilt "
                              "within the last day")
                : "Season list rebuilt: "
                      + std::to_string(
                            warcraftlogs::parseAllSeasonZones(zonesJson).size())
                      + " zones";
        AWOW_LOG_INFO("RANKINGS", message + " (" + cachePath.string() + ")");
        if (uiManager_) uiManager_->setLandingStatus(message);
        return true;
    }

    if (zones.empty()) {
        const std::string message =
            std::string("No current ") + contentStratum(content)
            + " season found in the listing";
        AWOW_LOG_ERROR("RANKINGS", message);
        if (uiManager_) uiManager_->setLandingStatus(message, true);
        return false;
    }

    size_t encounterCount = 0;
    for (const auto& zone : zones) encounterCount += zone.encounters.size();

    // Kept beside the rest of the WCL cache rather than in a folder the user
    // picks: a copy somewhere new each time would never be found again, and
    // the whole point is that a second baseline costs no queries.
    const auto rankingsDir = std::filesystem::path("wcl_cache") / "rankings";

    // Named with the API spelling so the file and the query that filled it
    // cannot disagree, and so the name carries no space.
    const auto rankingsFileFor = [&](auto encounterId) {
        return rankingsDir /
            (std::to_string(encounterId) + "_" + picked.apiClassName + "_"
             + picked.specName + ".json");
    };

    // Mythic for a raid and the single Dungeon difficulty for keystones, so a
    // baseline is taken at the level people actually push. A dummy zone offers
    // Normal and nothing else, which is why it is not a level at all.
    const int32_t difficulty = difficultyFor(content);

    size_t pulled = 0;
    size_t fromCache = 0;
    std::string firstError;
    for (const auto& zone : zones) {
        for (const auto& enc : zone.encounters) {
            const auto path = rankingsFileFor(enc.id);

            // Class and spec go to the server; the hero tree does not - the
            // rankings query has no argument for it, so it filters the replies
            // afterwards and is recorded here only so the caller knows which
            // baseline these files belong to.
            bool wasCached = false;
            const std::string err = wclSession_->loadCharacterRankings(
                enc.id, picked.apiClassName, picked.specName, difficulty,
                path.string(), &wasCached);

            if (err.empty()) {
                ++pulled;
                if (wasCached) ++fromCache;
                AWOW_LOG_INFO("RANKINGS", std::string("  ") + enc.name
                                              + (wasCached ? " (cached)" : "")
                                              + " -> " + path.string());
            } else {
                AWOW_LOG_ERROR("RANKINGS", enc.name + ": " + err);
                if (firstError.empty()) firstError = err;
            }
        }
    }

    // The files on disk are raw replies. This turns them into the thing a
    // reader can actually work from: flat tables plus a handoff that says what
    // the numbers are and how to use them beside an action priority list.
    warcraftlogs::RankingSample sample;
    for (const auto& zone : zones) {
        for (const auto& enc : zone.encounters) {
            std::ifstream in(rankingsFileFor(enc.id), std::ios::binary);
            if (!in.is_open()) continue;

            const std::string json((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            const std::string parseError =
                warcraftlogs::parseRankingReply(json, enc.id, sample);
            if (!parseError.empty()) {
                AWOW_LOG_WARNING("RANKINGS", enc.name + ": " + parseError);
            }
        }
    }

    if (!sample.pulls.empty() &&
        buildBaselineFromSample(sample, picked, zones.front().name)) {
        return true;
    }

    const std::string message =
        pulled == encounterCount
            ? "Ready: " + std::to_string(pulled) + " " + contentUnit(content)
                  + (fromCache ? " (" + std::to_string(fromCache)
                                     + " from cache)" : "")
            : "Pulled " + std::to_string(pulled) + " of "
                  + std::to_string(encounterCount) + " - " + firstError;

    AWOW_LOG_INFO("RANKINGS", message);
    if (uiManager_) uiManager_->setLandingStatus(message, pulled != encounterCount);
    return pulled > 0;
}

bool Application::runPackageForAnalysis() {
    AWOW_LOG_INFO("PACKAGE", "Opening file browser...");
    const std::string filepath = FileDialog::openCombatLogFile();
    if (filepath.empty()) {
        AWOW_LOG_INFO("PACKAGE", "No file selected. Returning to landing page.");
        return true;   // cancelled, not failed
    }

    if (!loadCombatLog(filepath)) {
        AWOW_LOG_ERROR("PACKAGE", "Failed to load combat log: " + filepath);
        return false;
    }

    const auto& encounters = logSession_->getEncounters();
    if (encounters.empty()) {
        AWOW_LOG_INFO("PACKAGE", "No encounters in this log; nothing to package.");
        clearCombatLog();
        return true;
    }

    const bool selectionMade = uiManager_->showEncounterSelectorWithRefresh(
        encounters,
        logSession_->getTotalLines(),
        logSession_->getScannedBytes(),
        logSession_->getTotalBytes(),
        [this]() -> bool { return logSession_->refresh(); });

    if (!selectionMade) {
        AWOW_LOG_INFO("PACKAGE", "No encounter picked. Returning to landing page.");
        clearCombatLog();
        return true;
    }

    // Refresh may have reordered things while the selector was open
    const auto& current = logSession_->getEncounters();
    const size_t index = uiManager_->getSelectedEncounterIndex();
    if (index >= current.size()) {
        AWOW_LOG_ERROR("PACKAGE", "Encounter index out of range after refresh");
        clearCombatLog();
        return false;
    }
    const auto& segment = current[index];

    auto lines = logSession_->getEncounterLines(index);
    if (lines.empty()) {
        AWOW_LOG_ERROR("PACKAGE", "Selected encounter tokenized to nothing");
        clearCombatLog();
        return false;
    }
    ExtractedData data = OutputWriter::extractAllEvents(lines);

    // Whose pull this is. COMBATANT_INFO names everyone who was there, and it
    // is already parsed, so there is no need to re-read the file for a roster.
    std::vector<PlayAlong::PlayerInfo> players;
    players.reserve(data.combatantInfoEvents.size());
    for (const auto& info : data.combatantInfoEvents) {
        auto nameIt = data.guidToNameMap.find(info.player_guid);
        players.push_back({info.player_guid,
                           nameIt == data.guidToNameMap.end() ? info.player_guid
                                                             : nameIt->second,
                           info.spec_id,
                           {}});
    }
    if (players.empty()) {
        AWOW_LOG_ERROR("PACKAGE",
                       "No COMBATANT_INFO in this encounter, so there is nobody to scope to");
        clearCombatLog();
        return false;
    }

    const auto picked = uiManager_->pickBenchmarkPlayer(players);
    if (!picked.selected) {
        AWOW_LOG_INFO("PACKAGE", "No character picked. Returning to landing page.");
        clearCombatLog();
        return true;
    }

    const std::string outputDir = FileDialog::selectPackageFolder();
    if (outputDir.empty()) {
        AWOW_LOG_INFO("PACKAGE", "No output folder chosen. Returning to landing page.");
        clearCombatLog();
        return true;
    }

    namespace pkg = awow::analysis;
    pkg::PackageRequest request;
    request.type = pkg::PackageType::ReviewEncounter;
    request.source = pkg::PackageSource::LocalLog;

    SpellNameDatabase::instance().loadSpellIds(data.collectAllSpellIds());
    request.resolveSpellName = shippedSpellNameResolver();

    pkg::FightData fight;
    fight.data = &data;
    fight.info = pkg::fightInfoFromSegment(
        segment, data, picked.playerGuid, static_cast<uint32_t>(index + 1),
        std::filesystem::path(filepath).filename().string());

    const auto result = pkg::buildPackage(outputDir, request, {fight});
    clearCombatLog();

    if (!result.ok) {
        AWOW_LOG_ERROR("PACKAGE", "Could not write the package: " + result.error);
        return false;
    }

    AWOW_LOG_INFO("PACKAGE", "Wrote " + std::to_string(result.fileCount)
                                 + " files to " + result.zipPath);
    return true;
}

bool Application::runLocalPlayAgain() {
    AWOW_LOG_INFO("PLAYAGAIN", "Opening file browser...");
    std::string filepath = FileDialog::openCombatLogFile();

    if (filepath.empty()) {
        AWOW_LOG_INFO("PLAYAGAIN", "No file selected. Returning to landing page.");
        return true;  // Not an error, just cancelled
    }

    if (!loadCombatLog(filepath)) {
        AWOW_LOG_ERROR("PLAYAGAIN", "Failed to load combat log: " + filepath);
        return false;
    }

    const auto& encounters = logSession_->getEncounters();

    if (encounters.empty()) {
        AWOW_LOG_INFO("PLAYAGAIN", "No encounters found in log. Cannot use Play Again mode.");
        clearCombatLog();
        return true;  // Not an error, just no encounters
    }

    bool selectionMade = uiManager_->showEncounterSelectorWithRefresh(
        encounters,
        logSession_->getTotalLines(),
        logSession_->getScannedBytes(),
        logSession_->getTotalBytes(),
        [this]() -> bool {
            AWOW_LOG_INFO("PLAYAGAIN", "Refreshing log...");
            bool hasNew = logSession_->refresh();
            if (hasNew) {
                AWOW_LOG_INFO("PLAYAGAIN", "Found new content!");
            }
            return hasNew;
        }
    );

    if (!selectionMade) {
        AWOW_LOG_INFO("PLAYAGAIN", "No selection made. Returning to landing page.");
        clearCombatLog();
        return true;
    }

    size_t selectedIndex = uiManager_->getSelectedEncounterIndex();
    const auto& currentEncounters = logSession_->getEncounters();

    if (selectedIndex >= currentEncounters.size()) {
        AWOW_LOG_ERROR("PLAYAGAIN", "Invalid encounter index: " + std::to_string(selectedIndex));
        clearCombatLog();
        return false;
    }

    const auto& selectedEncounter = currentEncounters[selectedIndex];
    AWOW_LOG_INFO("PLAYAGAIN", "Loading encounter: " + selectedEncounter.encounterName);
    publishCurrentEncounter(selectedEncounter);

    // On-demand: tokenize only the selected encounter's byte range
    auto linesToParse = logSession_->getEncounterLines(selectedIndex);
    std::string encounterName = selectedEncounter.encounterName
                              + " (" + getDifficultyName(selectedEncounter.difficultyId) + ")";

    ExtractedData extractedData = OutputWriter::extractAllEvents(linesToParse);

    AWOW_LOG_INFO("PLAYAGAIN", "Parsed " + std::to_string(extractedData.actorEvents.size()) + " position events, "
              + std::to_string(extractedData.spellCastEvents.size()) + " spell cast events, "
              + std::to_string(extractedData.combatantInfoEvents.size()) + " combatant info events");

    EncounterDataLoader loader;
    loader.setPerformanceMonitoring(false);  // Play Along is simpler, skip perf logging
    DatabaseBundle bundle = loader.loadFromExtractedData(extractedData);

    // Release mmap after strings are interned
    linesToParse.clear();
    logSession_->releaseFileMapping();

    if (!bundle.isValid()) {
        AWOW_LOG_ERROR("PLAYAGAIN", "Failed to create database bundle");
        clearCombatLog();
        return false;
    }

    // Load spell icon mappings for UI (spell names already loaded by loader)
    auto spellIds = extractedData.collectAllSpellIds();
    uiManager_->loadSpellDataForIds(spellIds);

    uiManager_->runPlayAlong(bundle.eventDb.get(), bundle.spellCastDb.get(),
                              bundle.combatDb.get(), bundle.auraDb.get(),
                              extractedData.combatantInfoEvents,
                              extractedData.mapChangeEvents, extractedData.deathEvents,
                              extractedData.resurrectEvents, &extractedData.spellNameMap,
                              encounterName);

    clearCombatLog();

    AWOW_LOG_INFO("PLAYAGAIN", "Play Again complete. Returning to landing page...");
    return true;
}

bool Application::runPlayAlongMode(const std::string& reportCode, int32_t fightId) {
    using namespace warcraftlogs;

    const std::string url = reportFightUrl(reportCode, fightId);
    const std::string cachedToken = storedWclAccessToken();

    wclSession_ = std::make_unique<WarcraftLogsSession>();
    wclSession_->setCacheEnabled(true, kWclCacheDir);

    if (!wclSession_->initialize(url)) {
        AWOW_LOG_ERROR("PLAYALONG", "Failed to parse URL '" + url + "': " + wclSession_->getLastError());
        wclSession_.reset();
        return false;
    }

    if (!cachedToken.empty()) {
        wclSession_->setUserAccessToken(cachedToken);
    }

    if (!wclSession_->fetchReport([this](const std::string& msg, float progress) {
        uiManager_->renderProgressFrame(msg, progress);
    })) {
        AWOW_LOG_ERROR("PLAYALONG", "Failed to fetch report '" + wclSession_->getReportCode() + "': " + wclSession_->getLastError());
        wclSession_.reset();
        return false;
    }

    std::string fightName = "Unknown Fight";
    int32_t encounterId = 0;
    int32_t difficulty = 0;
    int32_t keystoneLevel = 0;
    int64_t fightDurationMs = 0;
    for (const auto& fight : wclSession_->getFights()) {
        if (fight.id == fightId) {
            fightName = fight.name;
            encounterId = fight.encounterID;
            difficulty = fight.difficulty;
            keystoneLevel = fight.keystoneLevel;
            fightDurationMs = fight.duration();
            break;
        }
    }

    // Global encounter info, read by map loading and trigger persistence
    CurrentEncounter::instance().set(
        fightName,
        static_cast<uint32_t>(encounterId),
        static_cast<uint16_t>(difficulty),
        0,  // mapId will be derived from mapChangeEvents
        static_cast<int32_t>(fightDurationMs)
    );

    if (keystoneLevel > 0) {
        CurrentEncounter::instance().setMythicPlusInfo(keystoneLevel);
    }

    ExtractedData extractedData;
    try {
        extractedData = wclSession_->loadFight(fightId, [this, &fightName](const std::string& /*msg*/, float progress) {
            uiManager_->renderProgressFrame("Loading " + fightName, progress);
        });
    } catch (const std::exception& e) {
        AWOW_LOG_ERROR("PLAYALONG", "Failed to load fight " + std::to_string(fightId) + ": " + std::string(e.what()));
        wclSession_.reset();
        return false;
    }

    AWOW_LOG_INFO("PLAYALONG", "Loaded " + std::to_string(extractedData.actorEvents.size()) + " position events, "
              + std::to_string(extractedData.combatantInfoEvents.size()) + " combatant info events");

    EncounterDataLoader loader;
    loader.setPerformanceMonitoring(false);  // Play Along is simpler, skip perf logging
    DatabaseBundle bundle = loader.loadFromExtractedData(extractedData);

    if (!bundle.isValid()) {
        AWOW_LOG_ERROR("PLAYALONG", "Failed to create database bundle");
        wclSession_.reset();
        return false;
    }

    // Load spell icon mappings for UI (spell names already loaded by loader)
    auto spellIds = extractedData.collectAllSpellIds();
    uiManager_->loadSpellDataForIds(spellIds);

    uiManager_->runPlayAlong(bundle.eventDb.get(), bundle.spellCastDb.get(),
                              bundle.combatDb.get(), bundle.auraDb.get(),
                              extractedData.combatantInfoEvents,
                              extractedData.mapChangeEvents, extractedData.deathEvents,
                              extractedData.resurrectEvents, &extractedData.spellNameMap,
                              fightName);

    wclSession_.reset();

    AWOW_LOG_INFO("PLAYALONG", "Play Along complete. Returning to landing page...");
    return true;
}

void Application::runOverlayMode() {
    AWOW_LOG_INFO("OVERLAY", "Launching overlay mode");

    try {
        OverlayApplication overlay;
        // No logs folder set - overlay will search common WoW paths
        int result = overlay.run();
        AWOW_LOG_INFO("OVERLAY", "Overlay mode exited with code: " + std::to_string(result));
    } catch (const std::exception& e) {
        AWOW_LOG_ERROR("OVERLAY", std::string("Overlay crashed with exception: ") + e.what());
    } catch (...) {
        AWOW_LOG_ERROR("OVERLAY", "Overlay crashed with unknown exception");
    }
}

void Application::runLiveCompareMode() {
    auto& session = benchmark::BenchmarkSession::instance();

    // Where WoW is writing. The overlay saved this the last time it ran, and
    // both modes watch the same folder, so there is nothing new to ask.
    std::filesystem::path logsFolder =
        SettingsCache::instance().get().overlayLogsFolder;
    if (logsFolder.empty() || !std::filesystem::exists(logsFolder)) {
        logsFolder = "C:/Program Files (x86)/World of Warcraft/_retail_/Logs";
    }
    if (!std::filesystem::exists(logsFolder)) {
        AWOW_LOG_ERROR("BENCHMARK",
            "No Logs folder to watch - run overlay mode once to pick one");
        return;
    }

    // May start empty. The first pull's roster is offered as a picker, and
    // the answer is kept here and in settings for every pull after.
    std::string selfName = session.selfPlayerName();

    // Raised by the watcher thread when a pull of the right boss finishes,
    // and lowered by the render loop once it has re-run the comparison. The
    // comparison itself re-reads the log from disk, which is far too slow to
    // do on the poll thread.
    std::atomic<bool> pullReady{false};
    std::mutex pendingMutex;
    struct PendingPull {
        std::string logPath;
        size_t startOffset = 0;
        size_t endOffset = 0;
        uint32_t encounterId = 0;
    };
    PendingPull pending;

    LiveLogManager watcher;
    watcher.setOnPullEnd([&](const PullSegment& segment) {
        // Only pulls of the boss the reference is for. Trash, other bosses
        // and non-encounter segments are what the watcher spends most of its
        // time seeing, and none of them have anything to compare against.
        if (!segment.isEncounter) return;
        if (segment.encounterId == 0) return;
        if (!session.hasMentorFor(segment.encounterId)) return;

        auto* logSession = watcher.getSession();
        if (!logSession) return;

        // Kills count here, unlike the overlay's banner. The card is being
        // read after the fact rather than flashed mid-raid, and "what did the
        // kill look like against the reference" is the more useful question.
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.logPath = logSession->getLogPath().string();
            pending.startOffset = segment.startByteOffset;
            pending.endOffset = segment.endByteOffset;
            pending.encounterId = segment.encounterId;
        }
        pullReady.store(true, std::memory_order_release);
    });

    if (!watcher.start(logsFolder)) {
        AWOW_LOG_ERROR("BENCHMARK",
            "Could not watch " + logsFolder.string());
        return;
    }

    AWOW_LOG_INFO("BENCHMARK",
        "Watching " + logsFolder.string() + " for " + session.mentorName());

    // Re-reading a raid pull takes seconds, so it runs on a worker rather
    // than on the render thread. Blocking there froze the window for the whole
    // parse, which looked like a hang at exactly the moment the app was
    // supposed to be showing new numbers.
    std::future<bool> comparing;
    std::string statusLine = LF("benchmark.live_waiting", session.mentorName().c_str());
    int pullsSeen = 0;

    // The card rebuilds its context every frame from the session, so swapping
    // in a new comparison behind it is all that "update the card" takes.
    uiManager_->runLiveCompareReport([&]() -> std::string {
        // A parse already running. Nothing else can start until it lands,
        // because it is writing the session the card reads from.
        if (comparing.valid()) {
            if (comparing.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
                return statusLine;
            }

            const bool ok = comparing.get();
            comparing = {};
            if (ok) {
                AWOW_LOG_INFO("BENCHMARK", "Report card moved to the newest pull");
                statusLine = LF("benchmark.live_showing", pullsSeen);
            } else {
                AWOW_LOG_WARNING("BENCHMARK", "A pull ended but would not compare");
                statusLine = L("benchmark.live_skipped");
            }
            return statusLine;
        }

        if (!pullReady.exchange(false, std::memory_order_acq_rel)) {
            return statusLine;
        }

        PendingPull pull;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pull = pending;
        }
        ++pullsSeen;

        // Nothing in a combat log says which character belongs to the person
        // sitting here, so the first pull of a session is where that gets
        // settled: show its roster and let them point at themselves. The
        // answer is saved, so this only happens once.
        if (selfName.empty()) {
            const auto roster = session.rosterForPull(
                pull.logPath, pull.startOffset, pull.endOffset);

            if (roster.empty()) {
                AWOW_LOG_WARNING("BENCHMARK",
                    "A pull ended but carried no roster to pick from");
                statusLine = L("benchmark.live_skipped");
                return statusLine;
            }

            std::vector<PlayAlong::PlayerInfo> players;
            players.reserve(roster.size());
            for (const auto& entry : roster) {
                players.push_back({entry.guid, entry.name, 0, {}});
            }

            const auto picked = uiManager_->pickBenchmarkPlayer(players);
            if (!picked.selected) {
                AWOW_LOG_INFO("BENCHMARK", "No character picked; nothing to measure");
                statusLine = L("benchmark.live_skipped");
                return statusLine;
            }

            session.setSelfPlayerName(picked.playerName);
            selfName = picked.playerName;
            AWOW_LOG_INFO("BENCHMARK", "Measuring " + selfName + " from here on");
        }

        AWOW_LOG_INFO("BENCHMARK", "Reading pull " + std::to_string(pullsSeen));
        statusLine = LF("benchmark.live_reading", pullsSeen);

        comparing = std::async(std::launch::async, [&session, pull, selfName]() {
            return session.compareLivePull(pull.logPath, pull.startOffset,
                                           pull.endOffset, pull.encounterId,
                                           selfName);
        });

        return statusLine;
    });

    // The worker writes the session, which is about to go out of scope here.
    if (comparing.valid()) comparing.wait();

    watcher.stop();
}

bool Application::runSnapshotImport(const std::string& filepath) {
    AWOW_LOG_INFO("SNAPSHOT", "Importing snapshot: " + filepath);

    // Ensure clean state before importing - clear any leftover data from previous sessions
    uiManager_->unloadDatabases();

    snapshot::SnapshotImporter importer;
    auto result = importer.importSnapshot(filepath, [this](const std::string& phase, float progress) {
        uiManager_->renderProgressFrame("Importing snapshot: " + phase, progress);
    });

    if (!result.success) {
        AWOW_LOG_ERROR("SNAPSHOT", "Import failed: " + result.errorMessage);
        return false;
    }

    if (!result.data) {
        AWOW_LOG_ERROR("SNAPSHOT", "Import succeeded but no data returned");
        return false;
    }

    AWOW_LOG_INFO("SNAPSHOT", "Snapshot imported successfully.");
    AWOW_LOG_INFO("SNAPSHOT", "  Encounter: " + result.encounterName);
    AWOW_LOG_INFO("SNAPSHOT", "  Time window: " + std::to_string(result.windowStartMs) + "ms - " + std::to_string(result.windowEndMs) + "ms");
    AWOW_LOG_INFO("SNAPSHOT", "  Position events: " + std::to_string(result.data->actorEvents.size()));
    AWOW_LOG_INFO("SNAPSHOT", "  Spell casts: " + std::to_string(result.data->spellCastEvents.size()));
    AWOW_LOG_INFO("SNAPSHOT", "  Annotations: " + std::to_string(result.annotations.size()));
    AWOW_LOG_INFO("SNAPSHOT", "  Triggers JSON: " + std::to_string(result.triggersJson.size()) + " bytes");

    // Global encounter info, read by trigger persistence and snapshot export
    CurrentEncounter::instance().set(
        result.encounterName,
        result.encounterId,
        result.difficulty,
        result.mapId,
        result.encounterDurationMs
    );

    std::string title = "AwowLab - " + result.encounterName + " (Snapshot)";
    uiManager_->setWindowTitle(title);

    EncounterDataLoader loader;
    loader.setPerformanceMonitoring(true);
    DatabaseBundle bundle = loader.loadFromExtractedData(*result.data);

    if (!bundle.isValid()) {
        AWOW_LOG_ERROR("SNAPSHOT", "Failed to create database bundle from snapshot data");
        return false;
    }

    loader.loadIntoUI(uiManager_.get(), bundle, *result.data, result.encounterId);

    // Load triggers and cached events directly into AutoAnnotationTriggerManager
    auto* triggerManager = uiManager_->getAutoAnnotationTriggerManager();
    if (triggerManager) {
        // These define the auto-annotation rules that created the cached events
        if (!result.triggersJson.empty()) {
            if (triggerManager->deserializeTriggersFromJson(result.triggersJson)) {
                AWOW_LOG_INFO("SNAPSHOT", "Loaded triggers from snapshot JSON (" + std::to_string(result.triggersJson.size()) + " bytes)");
            } else {
                AWOW_LOG_WARNING("SNAPSHOT", "Failed to parse triggers JSON from snapshot");
            }
        }

        // This restores pre-computed trigger results so annotations display correctly
        if (!result.cachedEvents.empty()) {
            triggerManager->setCachedEvents(result.cachedEvents);
            AWOW_LOG_INFO("SNAPSHOT", "Loaded " + std::to_string(result.cachedEvents.size()) + " cached annotation events");
        }
    }

    if (!result.annotations.empty()) {
        AWOW_LOG_INFO("SNAPSHOT", "Snapshot contains " + std::to_string(result.annotations.size()) + " manual annotations.");
    }

    uiManager_->setExportCallback(exportSnapshot);

    uiManager_->resetNavigationFlags();
    uiManager_->run();

    // Check if we exited because a new file was received
    if (uiManager_->shouldLoadPendingFile() && singleInstance_) {
        // Reset flag before getting the file to allow subsequent loads
        uiManager_->resetNavigationFlags();

        std::string pendingPath = singleInstance_->getAndClearPendingFile();
        if (!pendingPath.empty()) {
            uiManager_->unloadDatabases();
            return runSnapshotImport(pendingPath);
        }
    }

    uiManager_->unloadDatabases();

    AWOW_LOG_INFO("SNAPSHOT", "Returning to landing page...");
    return true;
}
