#pragma once
#include <vector>
#include <functional>
#include <cstdint>

struct EncounterSegment;

class UIEncounterSelector {
public:
    UIEncounterSelector();

    // Set the list of encounters to display
    void setEncounters(const std::vector<EncounterSegment>& encounters);

    // Render the encounter selection UI
    // Returns true if an encounter was selected
    bool render();

    // Get the index of the selected encounter (call after render() returns true)
    size_t getSelectedEncounterIndex() const { return selectedIndex_; }

    // Check if user requested a refresh
    bool isRefreshRequested() const { return refreshRequested_; }

    // Reset the refresh flag (call after handling the refresh)
    void clearRefreshRequest() { refreshRequested_ = false; }

    // Check if user requested to return to home/landing page
    bool isReturnToHomeRequested() const { return returnToHomeRequested_; }

    // Set file info to display (for showing progress/stats)
    void setFileInfo(size_t totalLines, size_t parsedBytes, size_t totalBytes);

private:
    std::vector<EncounterSegment> encounters_;
    size_t selectedIndex_;
    bool encounterSelected_;
    bool refreshRequested_;
    bool returnToHomeRequested_;

    // File info for display
    size_t totalLines_;
    size_t parsedBytes_;
    size_t totalBytes_;

    // Helper to get difficulty name from difficulty ID
    static const char* getDifficultyName(uint32_t difficultyId);
};
