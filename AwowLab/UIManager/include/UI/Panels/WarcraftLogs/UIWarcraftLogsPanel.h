#pragma once

#include <vector>
#include <string>
#include <functional>
#include <cstdint>

namespace warcraftlogs {
    struct WclFight;
    class WarcraftLogsSession;
}

/**
 * Panel for selecting fights from a WarcraftLogs report.
 *
 * Displayed after the user enters a WarcraftLogs URL and the report
 * metadata has been fetched. Shows a list of fights with their
 * name, duration, difficulty, and cache status.
 */
class UIWarcraftLogsPanel {
public:
    UIWarcraftLogsPanel();

    /**
     * Set the session to display fights from.
     */
    void setSession(warcraftlogs::WarcraftLogsSession* session);

    /**
     * Render the fight selection UI.
     * @return True if a fight was selected.
     */
    bool render();

    /**
     * Get the ID of the selected fight.
     * @return Fight ID, or -1 if none selected.
     */
    [[nodiscard]] int32_t getSelectedFightId() const { return selectedFightId_; }

    /**
     * Check if user cancelled the selection.
     */
    [[nodiscard]] bool isCancelled() const { return cancelled_; }

    /**
     * Reset the panel state.
     */
    void reset();

    /**
     * Set progress message and value for loading indicator.
     * @param progress Progress 0.0-1.0, or -1 for indeterminate.
     */
    void setProgress(const std::string& message, float progress);

    /**
     * Check if the panel is currently showing loading progress.
     */
    [[nodiscard]] bool isLoading() const { return isLoading_; }

    /**
     * Set loading state.
     */
    void setLoading(bool loading) { isLoading_ = loading; }

private:
    warcraftlogs::WarcraftLogsSession* session_ = nullptr;
    int32_t selectedFightId_ = -1;
    bool fightSelected_ = false;
    bool cancelled_ = false;
    bool isLoading_ = false;

    // Progress tracking
    std::string progressMessage_;
    float progressValue_ = 0.0f;

    // Helper to get difficulty name
    static const char* getDifficultyName(int32_t difficultyId);
};
