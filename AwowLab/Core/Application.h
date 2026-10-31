#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include "UIManager/include/UI/UISettings.h"
#include "UnifiedSettings.h"

struct ExtractedData;
class UIManager;
class LogSession;

namespace awow {
    class SingleInstance;
}

namespace warcraftlogs {
    class WarcraftLogsSession;
    struct RankingSample;
}

namespace baseline_ui {
    struct SpecSelection;
    // Declared rather than included: this header is reached from most of the
    // application and the selector's is a UI header. The underlying type is
    // fixed there so this stays legal.
    enum class BaselineContent : uint8_t;
}

class Application {
public:
    Application(int width, int height, const std::string& title);
    ~Application();

    // Non-copyable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Run the application, returns exit code
    int run();

    // Set initial snapshot to open (skips landing page)
    void setInitialSnapshot(const std::string& filepath);

private:
    bool loadCombatLog(const std::string& filepath);
    bool runEncounterLoop();
    bool runLocalPlayAgain();  // Play Again mode for local files

    // Pick a combat log, pick an encounter in it, pick whose pull it is, then
    // write that fight out as a zip for an AI to read. Reuses the file dialog
    // and encounter selector the replay flow uses, so the only new decision is
    // which player the package is about.
    bool runPackageForAnalysis();

    // Pull the top-ranked players for a boss and save the reply. Baseline
    // packages are built from rankings, and no cached report contains one, so
    // this exists to capture the shape before anything parses it.
    bool runFetchRankings(baseline_ui::BaselineContent content,
                          bool rebuildOnly);

    // Turn a ranking sample into a baseline package. The rankings only chose
    // which fights to take; the package itself is the ordinary thirteen tables
    // per fight, so a baseline reads exactly like any other export and can be
    // analysed the same way.
    bool buildBaselineFromSample(const warcraftlogs::RankingSample& sample,
                                 const baseline_ui::SpecSelection& picked,
                                 const std::string& zoneName);
    // Import the snapshot another instance handed over, if it sent one.
    // True when a snapshot was opened and viewed.
    bool importPendingSnapshot();

    bool runWarcraftLogsFlow(const std::string& urlOrCode, const std::string& cachedAccessToken = "");
    bool runWarcraftLogsFlowWithFight(const std::string& reportCode, int32_t fightId);

    // Build a reference from one picked player in already-parsed events, walk
    // the user through classification, then re-cut the profile so the fresh
    // rulings reach the numbers. Shared by the local-log and report prepare
    // paths; the origin comes from whatever setLoadedSource recorded when the
    // log was opened.
    void prepareMentorFrom(const ExtractedData& data,
                           const std::string& playerGuid,
                           const std::string& playerName,
                           uint16_t specId);

    // Re-read the events a stored reference was cut from, following the
    // origin it recorded (a cached report or a local log). False when the
    // origin is missing or the source could not be reached; `data` is only
    // meaningful when it returns true.
    bool loadMentorSourceEvents(ExtractedData& data);

    // Re-open the log a stored reference was cut from and walk the prepare
    // steps against it again.
    //
    // The profile records where it came from precisely so this is possible
    // without asking the user to find the same report, fight and player a
    // second time. Returns false when the origin is missing (an older profile
    // saved before origins were recorded) or the source could not be reached.
    bool reopenMentorPrepare();

    // Show the report card, then act on any rebuild it asked for. The card
    // can only request; re-opening a log or report is this layer's job.
    void showBenchmarkReport();
    bool runPlayAlongMode(const std::string& reportCode, int32_t fightId);
    bool runSnapshotImport(const std::string& filepath);
    void runOverlayMode();  // Launch real-time combat overlay

    // Watch the live combat log and keep the report card on the newest pull
    // of the reference's encounter.
    //
    // No overlay: the only thing being watched for is "a pull of that boss
    // ended", at which point the comparison is re-run and the card - which
    // rebuilds from the session every frame - shows the new numbers.
    void runLiveCompareMode();
    void loadSettings();
    void syncRateLimitToCache();
    // Charges the session's queries against the shared local quota
    void attachRateLimit(warcraftlogs::WarcraftLogsSession& session);
    void clearCombatLog();

    int width_;
    int height_;
    std::string title_;

    std::unique_ptr<UIManager> uiManager_;
    std::unique_ptr<LogSession> logSession_;  // Manages log parsing state
    std::unique_ptr<warcraftlogs::WarcraftLogsSession> wclSession_;  // WarcraftLogs API session
    std::unique_ptr<awow::SingleInstance> singleInstance_;  // Single-instance enforcement

    // WarcraftLogs rate limit state (synced to SettingsCache)
    WclRateLimitState wclRateLimitState_;

    // Initial snapshot to load (set via command-line argument)
    std::string initialSnapshotPath_;
};
