#pragma once

#include "UI/Panels/WarcraftLogs/UIWarcraftLogsUrlDialog.h"
#include "UI/Panels/Navigation/UICachedFightsPanel.h"
#include "UI/Benchmark/UIMentorPicker.h"
#include "Core/Version.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

class ClassIconCache;
struct WclRateLimitState;
struct LandingPageRateLimitInfo;

namespace warcraftlogs {
    class ResponseCache;
}

enum class LandingPageChoice {
    None,
    LocalFile,
    LocalPlayAgain, // Play Again mode - practice with local logs
    WarcraftLogs,
    CachedFight,    // User selected a cached fight
    PlayAlong,      // Play Along mode - practice encounters
    ImportSnapshot, // Import .awsnap file
    LoadPendingFile, // Load file received from another instance
    ExitForUpdate,  // Exit app to run updater
    OverlayMode,    // Launch real-time combat overlay
    // Compare a pull against a reference. Preparing a reference comes first -
    // the other two are inert until at least one exists.
    PrepareMentor,      // Cached report -> pick a player -> reference
    PrepareMentorLocal, // Same, from a combat log on disk
    CompareReports,  // Reference vs a recorded log
    CompareLive,     // Reference vs each pull as it happens
    // Export a fight's tables as a zip. The fight is picked with the same
    // cached-report and local-log pickers the rest of the page uses.
    PackageForAnalysis,
    // Start a baseline by pulling the season's rankings. Dungeons and raids are
    // separate choices because WCL ranks them differently - a keystone run is
    // ranked per dungeon, a boss per encounter - so the query differs.
    FetchRankingsDungeon,
    FetchRankingsRaid,
    // Refetch the season list. Only needed when a patch changes what is being
    // run, which the user knows and the app does not.
    RebuildSeasonList,
    Exit
};

struct LandingPageResult {
    LandingPageChoice choice = LandingPageChoice::None;
    std::string warcraftLogsUrl;  // If WarcraftLogs chosen, contains the URL/report ID
    bool useUserAuth = false;     // Whether to use user OAuth token
    std::string accessToken;      // User access token if authenticated

    // Cached fight selection (when choice == CachedFight)
    std::string cachedReportCode;
    int32_t cachedFightId = -1;

    // Which stored reference the comparison should run against. Set for
    // CompareReports and CompareLive; the encounter also tells CompareReports
    // which cached fights are worth offering as the log to measure.
    uint32_t benchmarkEncounterId = 0;
    uint16_t benchmarkSpecId = 0;
};

class UILandingPage {
public:
    UILandingPage();
    ~UILandingPage() = default;

    // Render the landing page. Returns true if a selection was made.
    bool render();

    // Get the user's choice
    LandingPageResult getResult() const { return result_; }

    // Report what a season-list or package action did, so the card can say so.
    // Cleared by reset() with the rest of the page state.
    void setPackageStatus(const std::string& message, bool isError = false) {
        packageStatus_ = message;
        packageStatusIsError_ = isError;
    }

    // Reset for re-display
    void reset();

    // Apply custom theme for the landing page
    static void applyLandingTheme();

private:
    void renderHeader();
    void renderSourceCards();
    void renderProjectLinks();
    void renderFooter();

    LandingPageResult result_;

    // WarcraftLogs URL dialog
    std::unique_ptr<UIWarcraftLogsUrlDialog> wclUrlDialog_;
    bool wclDialogOpen_ = false;

    // Cached fights panel
    std::unique_ptr<UICachedFightsPanel> cachedFightsPanel_;
    bool cachedFightsPanelOpen_ = false;

    // Animation state
    float animationTime_ = 0.0f;
    int hoveredCard_ = -1;

    // Play Along mode pending (after cached fight selection)
    bool playAlongPending_ = false;

    // Preparing a reference browses the whole cache through the same panel the
    // WarcraftLogs card uses, so this remembers who asked - without it the
    // selection would fall through to the plain replay path.
    bool prepareMentorPending_ = false;

    // What the last season-list action did, shown on the package card. Without
    // it a rebuild looks identical to nothing happening - the file is written
    // somewhere the user is not looking.
    std::string packageStatus_;
    bool packageStatusIsError_ = false;

    // Encounter-first picker for the two comparison flows. Mode 0 stops at the
    // reference (live), mode 1 goes on to pick the pull to measure.
    bool mentorPickerOpen_ = false;
    int mentorPickerMode_ = 0;
    std::unique_ptr<benchmark_ui::UIMentorPicker> mentorPicker_;

    // Rate limit state for WarcraftLogs (passed from Application)
    WclRateLimitState* rateLimitState_ = nullptr;
    bool hasCustomCredentials_ = false;

    // Class icon cache for cached fights panel
    ClassIconCache* classIconCache_ = nullptr;

    // Unlock instructions popup state
    bool showUnlockInstructions_ = false;
    char inputClientId_[128] = "";
    char inputClientSecret_[128] = "";
    std::string credentialSaveError_;
    bool credentialsSaved_ = false;
    bool isValidating_ = false;

    // Credential validation callback (provided by Application)
    std::function<bool(const std::string&, const std::string&, std::string&)> validateCredentialsCallback_;

    // Auto-open the WCL dialog on next render (for returning after auth failure)
    bool autoOpenWclDialog_ = false;

    // Update state
    bool updateCheckRequested_ = false;
    std::string updaterArgs_;  // Arguments to pass to updater

    // Language pack download prompt state
    bool languagePackPromptDismissed_ = false;  // User clicked "No thanks" this session

    // Helper to launch the updater executable
    void launchUpdater(const std::string& args);

    // Render optional language pack download card (shown when pack is missing)
    void renderLanguagePackCard();

public:
    // Set rate limit state for WarcraftLogs display
    void setRateLimitState(WclRateLimitState* state, bool hasCustomCredentials) {
        rateLimitState_ = state;
        hasCustomCredentials_ = hasCustomCredentials;
    }

    // Set callback to validate credentials before saving
    // Callback returns true if valid, false otherwise. Error message in outError.
    void setValidateCredentialsCallback(
        std::function<bool(const std::string&, const std::string&, std::string&)> callback) {
        validateCredentialsCallback_ = std::move(callback);
    }

    // Set response cache for browsing cached fights
    void setResponseCache(warcraftlogs::ResponseCache* cache);

    // Set class icon cache for cached fights panel
    void setClassIconCache(ClassIconCache* cache) { classIconCache_ = cache; }

    // Get number of cached fights (for UI display)
    size_t getCachedFightCount() const;

    // Mark the WCL token as invalid due to a query failure
    // This will show the reauthorize button next time the dialog opens
    void markWclTokenInvalid(const std::string& errorMessage);

    // Auto-open the WCL dialog on next render (for returning after auth failure)
    void setAutoOpenWclDialog(bool autoOpen) { autoOpenWclDialog_ = autoOpen; }
};
