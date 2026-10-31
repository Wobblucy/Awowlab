#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <unordered_map>

class AuraDatabase;
class PlaybackController;

// Debug panel for viewing buff/debuff tracking data
// Allows filtering by target GUID/name and displays all auras with durations
class UIAuraDebugPanel {
public:
    UIAuraDebugPanel();
    ~UIAuraDebugPanel() = default;

    void render(
        const AuraDatabase* auraDb,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime,
        std::shared_ptr<PlaybackController> playback
    );

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    bool* getVisiblePtr() { return &visible_; }

private:
    bool visible_ = false;  // Hidden by default (debug panel)
    char filterText_[256] = "";  // Filter by GUID or name
    bool showBuffs_ = true;
    bool showDebuffs_ = true;
    bool showActiveOnly_ = true;  // Only show auras active at current time

    // Format duration as seconds with 1 decimal
    static std::string formatDuration(uint32_t duration_ms);
};
