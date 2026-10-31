#pragma once
#include <imgui.h>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Core/UnifiedSettings.h"

// Anchor point for window positioning (relative to viewport)
enum class WindowAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

// Stores position and visibility state for a single UI window
struct WindowState {
    WindowAnchor anchor = WindowAnchor::TopLeft;
    ImVec2 offsetFromAnchor = ImVec2(10.0f, 30.0f);  // Offset from anchor point
    ImVec2 size = ImVec2(400.0f, 300.0f);
    bool visible = true;
    bool firstFrame = true;  // Track first frame to apply saved position

    // Calculate absolute position from anchor and offset
    ImVec2 getAbsolutePosition(const ImGuiViewport* viewport) const {
        ImVec2 pos;

        switch (anchor) {
            case WindowAnchor::TopLeft:
                pos.x = viewport->Pos.x + offsetFromAnchor.x;
                pos.y = viewport->Pos.y + offsetFromAnchor.y;
                break;
            case WindowAnchor::TopRight:
                pos.x = viewport->Pos.x + viewport->Size.x - size.x - offsetFromAnchor.x;
                pos.y = viewport->Pos.y + offsetFromAnchor.y;
                break;
            case WindowAnchor::BottomLeft:
                pos.x = viewport->Pos.x + offsetFromAnchor.x;
                pos.y = viewport->Pos.y + viewport->Size.y - size.y - offsetFromAnchor.y;
                break;
            case WindowAnchor::BottomRight:
                pos.x = viewport->Pos.x + viewport->Size.x - size.x - offsetFromAnchor.x;
                pos.y = viewport->Pos.y + viewport->Size.y - size.y - offsetFromAnchor.y;
                break;
        }

        return pos;
    }

    // Update anchor and offset based on current absolute position
    void updateFromAbsolutePosition(ImVec2 absPos, const ImGuiViewport* viewport) {
        // Determine which corner the window is closest to
        float distToTopLeft = std::abs(absPos.x - viewport->Pos.x) +
                              std::abs(absPos.y - viewport->Pos.y);
        float distToTopRight = std::abs((absPos.x + size.x) - (viewport->Pos.x + viewport->Size.x)) +
                               std::abs(absPos.y - viewport->Pos.y);
        float distToBottomLeft = std::abs(absPos.x - viewport->Pos.x) +
                                 std::abs((absPos.y + size.y) - (viewport->Pos.y + viewport->Size.y));
        float distToBottomRight = std::abs((absPos.x + size.x) - (viewport->Pos.x + viewport->Size.x)) +
                                  std::abs((absPos.y + size.y) - (viewport->Pos.y + viewport->Size.y));

        // Find closest corner
        float minDist = distToTopLeft;
        anchor = WindowAnchor::TopLeft;

        if (distToTopRight < minDist) {
            minDist = distToTopRight;
            anchor = WindowAnchor::TopRight;
        }
        if (distToBottomLeft < minDist) {
            minDist = distToBottomLeft;
            anchor = WindowAnchor::BottomLeft;
        }
        if (distToBottomRight < minDist) {
            anchor = WindowAnchor::BottomRight;
        }

        // Calculate offset from chosen anchor
        switch (anchor) {
            case WindowAnchor::TopLeft:
                offsetFromAnchor.x = absPos.x - viewport->Pos.x;
                offsetFromAnchor.y = absPos.y - viewport->Pos.y;
                break;
            case WindowAnchor::TopRight:
                offsetFromAnchor.x = (viewport->Pos.x + viewport->Size.x) - (absPos.x + size.x);
                offsetFromAnchor.y = absPos.y - viewport->Pos.y;
                break;
            case WindowAnchor::BottomLeft:
                offsetFromAnchor.x = absPos.x - viewport->Pos.x;
                offsetFromAnchor.y = (viewport->Pos.y + viewport->Size.y) - (absPos.y + size.y);
                break;
            case WindowAnchor::BottomRight:
                offsetFromAnchor.x = (viewport->Pos.x + viewport->Size.x) - (absPos.x + size.x);
                offsetFromAnchor.y = (viewport->Pos.y + viewport->Size.y) - (absPos.y + size.y);
                break;
        }
    }
};

// Window size settings (separate from UI layout for early loading)
struct WindowSizeSettings {
    int width = 0;   // 0 means use default (80% of screen)
    int height = 0;
    bool maximized = false;

    // Save window size to cached settings (auto-saves after 10s)
    bool saveToFile([[maybe_unused]] const std::string& filename = "") const {
        auto& cache = SettingsCache::instance();
        cache.get().windowWidth = width;
        cache.get().windowHeight = height;
        cache.get().windowMaximized = maximized;
        cache.markDirty();
        return true;
    }

    // Load window size from cached settings
    bool loadFromFile([[maybe_unused]] const std::string& filename = "") {
        const auto& settings = SettingsCache::instance().get();

        width = settings.windowWidth;
        height = settings.windowHeight;
        maximized = settings.windowMaximized;

        return (width > 0 && height > 0);
    }
};

// Global UI settings including window states and preferences
struct UISettings {
    // Window states
    WindowState playbackControls;
    WindowState creaturesPanel;
    WindowState filtersPanel;
    WindowState minimapWindow;
    WindowState actorSelector;
    WindowState spellHistory;
    WindowState raidFramePanel;
    WindowState enemyGridPanel;
    WindowState meterPanel1;   // Meter 1 (visible by default)
    WindowState meterPanel2;   // Meter 2
    WindowState meterPanel3;   // Meter 3
    WindowState upcomingBossCastsPanel;
    WindowState settingsHub;

    // Menu bar visibility
    bool showMenuBar = true;

    // Default constructor sets default window positions using anchors
    UISettings() {
        // Playback Controls - Top-left corner
        playbackControls.anchor = WindowAnchor::TopLeft;
        playbackControls.offsetFromAnchor = ImVec2(10.0f, 30.0f);
        playbackControls.size = ImVec2(400.0f, 200.0f);

        // Creatures Panel - Below playback controls (top-left) - Hidden by default
        creaturesPanel.visible = false;
        creaturesPanel.anchor = WindowAnchor::TopLeft;
        creaturesPanel.offsetFromAnchor = ImVec2(10.0f, 250.0f);
        creaturesPanel.size = ImVec2(300.0f, 400.0f);

        // Filters Panel - Top-left, to the right of playback
        filtersPanel.anchor = WindowAnchor::TopLeft;
        filtersPanel.offsetFromAnchor = ImVec2(420.0f, 30.0f);
        filtersPanel.size = ImVec2(300.0f, 350.0f);

        // Minimap - Top-right corner, hidden by default
        minimapWindow.visible = false;
        minimapWindow.anchor = WindowAnchor::TopRight;
        minimapWindow.offsetFromAnchor = ImVec2(10.0f, 30.0f);
        minimapWindow.size = ImVec2(400.0f, 400.0f);

        // Actor Selector - Hidden by default (selection handled by Raid Frames)
        actorSelector.visible = false;
        actorSelector.anchor = WindowAnchor::TopLeft;
        actorSelector.offsetFromAnchor = ImVec2(420.0f, 390.0f);
        actorSelector.size = ImVec2(300.0f, 300.0f);

        // Spell History - Bottom-right corner - Hidden by default
        spellHistory.visible = false;
        spellHistory.anchor = WindowAnchor::BottomRight;
        spellHistory.offsetFromAnchor = ImVec2(10.0f, 10.0f);
        spellHistory.size = ImVec2(400.0f, 200.0f);

        // Raid Frames - Bottom-left corner
        raidFramePanel.anchor = WindowAnchor::BottomLeft;
        raidFramePanel.offsetFromAnchor = ImVec2(10.0f, 10.0f);
        raidFramePanel.size = ImVec2(480.0f, 430.0f);

        // Enemy Grid - Bottom-right, above spell history - Hidden by default
        enemyGridPanel.visible = false;
        enemyGridPanel.anchor = WindowAnchor::BottomRight;
        enemyGridPanel.offsetFromAnchor = ImVec2(10.0f, 220.0f);
        enemyGridPanel.size = ImVec2(480.0f, 300.0f);

        // Meter 1 - Visible by default, right side below minimap
        meterPanel1.visible = true;
        meterPanel1.anchor = WindowAnchor::TopRight;
        meterPanel1.offsetFromAnchor = ImVec2(10.0f, 440.0f);
        meterPanel1.size = ImVec2(400.0f, 400.0f);

        // Meter 2 - Hidden by default
        meterPanel2.visible = false;
        meterPanel2.anchor = WindowAnchor::TopRight;
        meterPanel2.offsetFromAnchor = ImVec2(420.0f, 440.0f);
        meterPanel2.size = ImVec2(400.0f, 400.0f);

        // Meter 3 - Hidden by default
        meterPanel3.visible = false;
        meterPanel3.anchor = WindowAnchor::TopRight;
        meterPanel3.offsetFromAnchor = ImVec2(420.0f, 850.0f);
        meterPanel3.size = ImVec2(400.0f, 400.0f);

        // Upcoming Boss Casts - Right edge, vertical bar
        upcomingBossCastsPanel.visible = true;
        upcomingBossCastsPanel.anchor = WindowAnchor::TopRight;
        upcomingBossCastsPanel.offsetFromAnchor = ImVec2(10.0f, 60.0f);
        upcomingBossCastsPanel.size = ImVec2(80.0f, 600.0f);

        // Settings Hub - Hidden by default, centered
        settingsHub.visible = false;
        settingsHub.anchor = WindowAnchor::TopLeft;
        settingsHub.offsetFromAnchor = ImVec2(100.0f, 100.0f);
        settingsHub.size = ImVec2(600.0f, 500.0f);
    }

    // Helper to convert anchor enum to string
    static const char* anchorToString(WindowAnchor anchor) {
        switch (anchor) {
            case WindowAnchor::TopLeft: return "TopLeft";
            case WindowAnchor::TopRight: return "TopRight";
            case WindowAnchor::BottomLeft: return "BottomLeft";
            case WindowAnchor::BottomRight: return "BottomRight";
            default: return "TopLeft";
        }
    }

    // Helper to convert string to anchor enum
    static WindowAnchor stringToAnchor(const std::string& str) {
        if (str == "TopRight") return WindowAnchor::TopRight;
        if (str == "BottomLeft") return WindowAnchor::BottomLeft;
        if (str == "BottomRight") return WindowAnchor::BottomRight;
        return WindowAnchor::TopLeft;
    }

    // Serialize UI layout to INI-format string
    std::string serializeToString() const {
        std::ostringstream ss;

        ss << "[PlaybackControls]\n";
        ss << "visible=" << playbackControls.visible << "\n";
        ss << "anchor=" << anchorToString(playbackControls.anchor) << "\n";
        ss << "offset=" << playbackControls.offsetFromAnchor.x << "," << playbackControls.offsetFromAnchor.y << "\n";
        ss << "size=" << playbackControls.size.x << "," << playbackControls.size.y << "\n\n";

        ss << "[CreaturesPanel]\n";
        ss << "visible=" << creaturesPanel.visible << "\n";
        ss << "anchor=" << anchorToString(creaturesPanel.anchor) << "\n";
        ss << "offset=" << creaturesPanel.offsetFromAnchor.x << "," << creaturesPanel.offsetFromAnchor.y << "\n";
        ss << "size=" << creaturesPanel.size.x << "," << creaturesPanel.size.y << "\n\n";

        ss << "[FiltersPanel]\n";
        ss << "visible=" << filtersPanel.visible << "\n";
        ss << "anchor=" << anchorToString(filtersPanel.anchor) << "\n";
        ss << "offset=" << filtersPanel.offsetFromAnchor.x << "," << filtersPanel.offsetFromAnchor.y << "\n";
        ss << "size=" << filtersPanel.size.x << "," << filtersPanel.size.y << "\n\n";

        ss << "[MinimapWindow]\n";
        ss << "visible=" << minimapWindow.visible << "\n";
        ss << "anchor=" << anchorToString(minimapWindow.anchor) << "\n";
        ss << "offset=" << minimapWindow.offsetFromAnchor.x << "," << minimapWindow.offsetFromAnchor.y << "\n";
        ss << "size=" << minimapWindow.size.x << "," << minimapWindow.size.y << "\n\n";

        ss << "[ActorSelector]\n";
        ss << "visible=" << actorSelector.visible << "\n";
        ss << "anchor=" << anchorToString(actorSelector.anchor) << "\n";
        ss << "offset=" << actorSelector.offsetFromAnchor.x << "," << actorSelector.offsetFromAnchor.y << "\n";
        ss << "size=" << actorSelector.size.x << "," << actorSelector.size.y << "\n\n";

        ss << "[SpellHistory]\n";
        ss << "visible=" << spellHistory.visible << "\n";
        ss << "anchor=" << anchorToString(spellHistory.anchor) << "\n";
        ss << "offset=" << spellHistory.offsetFromAnchor.x << "," << spellHistory.offsetFromAnchor.y << "\n";
        ss << "size=" << spellHistory.size.x << "," << spellHistory.size.y << "\n\n";

        ss << "[RaidFramePanel]\n";
        ss << "visible=" << raidFramePanel.visible << "\n";
        ss << "anchor=" << anchorToString(raidFramePanel.anchor) << "\n";
        ss << "offset=" << raidFramePanel.offsetFromAnchor.x << "," << raidFramePanel.offsetFromAnchor.y << "\n";
        ss << "size=" << raidFramePanel.size.x << "," << raidFramePanel.size.y << "\n\n";

        ss << "[EnemyGridPanel]\n";
        ss << "visible=" << enemyGridPanel.visible << "\n";
        ss << "anchor=" << anchorToString(enemyGridPanel.anchor) << "\n";
        ss << "offset=" << enemyGridPanel.offsetFromAnchor.x << "," << enemyGridPanel.offsetFromAnchor.y << "\n";
        ss << "size=" << enemyGridPanel.size.x << "," << enemyGridPanel.size.y << "\n\n";

        ss << "[MeterPanel1]\n";
        ss << "visible=" << meterPanel1.visible << "\n";
        ss << "anchor=" << anchorToString(meterPanel1.anchor) << "\n";
        ss << "offset=" << meterPanel1.offsetFromAnchor.x << "," << meterPanel1.offsetFromAnchor.y << "\n";
        ss << "size=" << meterPanel1.size.x << "," << meterPanel1.size.y << "\n\n";

        ss << "[MeterPanel2]\n";
        ss << "visible=" << meterPanel2.visible << "\n";
        ss << "anchor=" << anchorToString(meterPanel2.anchor) << "\n";
        ss << "offset=" << meterPanel2.offsetFromAnchor.x << "," << meterPanel2.offsetFromAnchor.y << "\n";
        ss << "size=" << meterPanel2.size.x << "," << meterPanel2.size.y << "\n\n";

        ss << "[MeterPanel3]\n";
        ss << "visible=" << meterPanel3.visible << "\n";
        ss << "anchor=" << anchorToString(meterPanel3.anchor) << "\n";
        ss << "offset=" << meterPanel3.offsetFromAnchor.x << "," << meterPanel3.offsetFromAnchor.y << "\n";
        ss << "size=" << meterPanel3.size.x << "," << meterPanel3.size.y << "\n\n";

        ss << "[UpcomingBossCastsPanel]\n";
        ss << "visible=" << upcomingBossCastsPanel.visible << "\n";
        ss << "anchor=" << anchorToString(upcomingBossCastsPanel.anchor) << "\n";
        ss << "offset=" << upcomingBossCastsPanel.offsetFromAnchor.x << "," << upcomingBossCastsPanel.offsetFromAnchor.y << "\n";
        ss << "size=" << upcomingBossCastsPanel.size.x << "," << upcomingBossCastsPanel.size.y << "\n\n";

        ss << "[SettingsHub]\n";
        ss << "visible=" << settingsHub.visible << "\n";
        ss << "anchor=" << anchorToString(settingsHub.anchor) << "\n";
        ss << "offset=" << settingsHub.offsetFromAnchor.x << "," << settingsHub.offsetFromAnchor.y << "\n";
        ss << "size=" << settingsHub.size.x << "," << settingsHub.size.y << "\n\n";

        return ss.str();
    }

    // Save UI layout to cached settings (auto-saves after 10s)
    bool saveToFile([[maybe_unused]] const std::string& filename = "") const {
        auto& cache = SettingsCache::instance();
        cache.get().uiLayoutJson = serializeToString();
        cache.markDirty();
        return true;
    }

    // Deserialize UI layout from INI-format string
    bool deserializeFromString(const std::string& data) {
        if (data.empty()) {
            return false;
        }

        std::istringstream stream(data);
        std::string line;
        WindowState* currentWindow = nullptr;

        while (std::getline(stream, line)) {
            // Skip empty lines
            if (line.empty()) continue;

            // Section headers
            if (line == "[PlaybackControls]") {
                currentWindow = &playbackControls;
            } else if (line == "[CreaturesPanel]") {
                currentWindow = &creaturesPanel;
            } else if (line == "[FiltersPanel]") {
                currentWindow = &filtersPanel;
            } else if (line == "[MinimapWindow]") {
                currentWindow = &minimapWindow;
            } else if (line == "[ActorSelector]") {
                currentWindow = &actorSelector;
            } else if (line == "[SpellHistory]") {
                currentWindow = &spellHistory;
            } else if (line == "[RaidFramePanel]") {
                currentWindow = &raidFramePanel;
            } else if (line == "[EnemyGridPanel]") {
                currentWindow = &enemyGridPanel;
            } else if (line == "[MeterPanel1]") {
                currentWindow = &meterPanel1;
            } else if (line == "[MeterPanel2]") {
                currentWindow = &meterPanel2;
            } else if (line == "[MeterPanel3]") {
                currentWindow = &meterPanel3;
            // Legacy support: map old meter panels to new meters
            } else if (line == "[UnifiedMeterPanel]" || line == "[CombatMeterPanel]") {
                currentWindow = &meterPanel1;  // Map legacy unified/combat to Meter 1
            } else if (line == "[HealingMeterPanel]") {
                currentWindow = &meterPanel2;  // Map legacy healing to Meter 2
            } else if (line == "[DamageTakenPanel]") {
                currentWindow = &meterPanel3;  // Map legacy damage taken to Meter 3
            } else if (line == "[UpcomingBossCastsPanel]") {
                currentWindow = &upcomingBossCastsPanel;
            } else if (line == "[SettingsHub]") {
                currentWindow = &settingsHub;
            } else if (line == "[Settings]") {
                currentWindow = nullptr;  // Settings section
            } else if (currentWindow != nullptr) {
                // Parse window properties
                if (line.substr(0, 8) == "visible=") {
                    currentWindow->visible = (line.substr(8) == "1");
                } else if (line.substr(0, 7) == "anchor=") {
                    currentWindow->anchor = stringToAnchor(line.substr(7));
                } else if (line.substr(0, 7) == "offset=") {
                    sscanf(line.substr(7).c_str(), "%f,%f",
                           &currentWindow->offsetFromAnchor.x, &currentWindow->offsetFromAnchor.y);
                } else if (line.substr(0, 5) == "size=") {
                    sscanf(line.substr(5).c_str(), "%f,%f",
                           &currentWindow->size.x, &currentWindow->size.y);
                }
            }
            // Global settings are stored separately in UnifiedSettings
        }

        return true;
    }

    // Load UI layout from cached settings
    bool loadFromFile([[maybe_unused]] const std::string& filename = "") {
        const auto& settings = SettingsCache::instance().get();
        return deserializeFromString(settings.uiLayoutJson);
    }

};
